/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    ev_timer.c
 * @brief   用户态定时器实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-25
 * @version 1.1
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-17 | cai | Initial creation.
 *   1.1 | 2026-06-25 | cai | Use event loop for ev_high_res_timer timerfd.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <unistd.h>
#include <time.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <sys/errno.h>
#include <sys/prctl.h>

#include "plat/atom.h"
#include "event/ev_timer.h"
#include "event/ev_thread.h"
#include "event/ev_lock.h"
#include "plat/debug.h"
#include "mp/mp.h"
#include "thp/thp.h"
#include "event/ev_loop.h"
#include "syslog/syslog.h"
#include "swdg/swdg.h"

/* ========================================================================== */
/*                            Macro Definitions                               */
/* ========================================================================== */

// 高精度定时器监听数量
#define EV_HIGH_RES_TIMER_COUNT_MAX     (64)

// 批量处理到期定时器个数
#define EV_TIMER_BATCH      (32)

// 高精度epoll等待时间
#define EV_HIGH_RES_EPOLL_WAIT      (200)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 定时器定义
struct ev_timer_s{
    char *name;                 // name
    uint64_t expired_time;      // 绝对超时时间
    uint32_t timeout;           // 超时时间
    ev_timer_cb_func cb;        // 回调函数
    void *args;                 // 回调参数
    ATOMIC_UINT8_T actived;     // 激活标记 
    ATOMIC_UINT8_T deleted;     // 删除标记
    ev_timer_heap_item_t item;  // heap item
};

// 高精度定时器定义
struct ev_high_res_timer_s{
    const char *name;   // name
    int tfd;        // timer fd
    uint64_t tv;    // timer value, ms
    ev_timer_cb_func cb;    // 回调
    void *args;             // 回调参数
};

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       compare two timer
 * 
 * @param[in]   t1  - timer 1
 * @param[in]   t2  - timer 2
 * 
 * @note        cmp by expired time
 */
static attr_pure_inline int _ev_timer_cmp(ev_timer_t *t1, ev_timer_t *t2)
{
    assert(t1 && t2);
    if(t1->expired_time < t2->expired_time)
        return -1;
    else if(t1->expired_time > t2->expired_time)
        return 1;
    return 0;
}

uint64_t mono_time_get()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ms = ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL;
    return ms;
}

/**
 * @brief       low resolution clock work function
 * 
 * @note        （1）通过mono time更新低精度时钟（2）检查堆顶，到期则执行cb，移出堆
 */
static void _low_res_clock_work(void *args);

/**
 * @brief       epoll routine for high res timer
 * 
 * @note        高精度定时器的tfd独立管理，不注册到ev_loop，避免调用开销太大影响精度
 */
static void* _ev_timer_high_res_epoll_main(void *args);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 低精度时钟更新线程
declare_ev_thd(low_res_clock, _low_res_clock_work, NULL, 0)
// 当前时间，ms表示
static ATOMIC_UINT64_T g_curr_ms = 0;
// 最小堆，管理定时器
static ev_timer_heap_head_t g_ev_timer_heap = {};
// 自旋锁，用来保护堆
static ev_spinlock_t g_heap_spinlock = EV_SPINLOCK_INITIALIZER;

// 启动时间，mono
static uint64_t g_startup_ms = 0;

// 低精度线程池
declare_thp(ev_timer, THREAD_POOL_TH_COUNT_MAX)
// 高精度线程池
declare_thp(ev_high_res_timer, THREAD_POOL_TH_COUNT_MAX)

// 高精度定时器epoll线程id
static pthread_t g_high_res_timer_epoll_tid = -1;
// epoll_fd
static int g_high_res_timer_epoll_fd = -1;
// 运行标志
static ATOMIC_UINT8_T g_high_res_timer_run;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

// 定义堆操作
declare_heap(ev_timer, ev_timer, ev_timer_t, item, 1, 1024, _ev_timer_cmp)

static void _low_res_clock_work(void *args)
{
    while(1)
    {
        // 不走ev_thd定时唤醒，需要手动喂狗
        swdg_feed();

        uint64_t ms = mono_time_get();
        ATOM_STORE(&g_curr_ms, ms, MORDER_RELEASE);
        ev_timer_t *expired_batch[EV_TIMER_BATCH] = {};
        int batch_count = 0;

        ev_with_spinlock(&g_heap_spinlock)
        {
            ev_timer_t *timer;

            while((batch_count < EV_TIMER_BATCH) && (timer = ev_timer_heap_top(&g_ev_timer_heap)))
            {
                if(ATOM_LOAD(&timer->deleted, MORDER_ACQUIRE))  // 存在deleted标记，说明定时器被取消了
                {
                    ev_timer_heap_pop(&g_ev_timer_heap);    // 移出堆
                    // 重置actived和deleted
                    ATOM_STORE(&timer->actived, 0, MORDER_RELEASE);
                    ATOM_STORE(&timer->deleted, 0, MORDER_RELEASE);
                    continue;
                }

                if(ms < timer->expired_time)
                    break;

                dbg("low res timer [%s] expired", timer->name);

                // 定时器移出堆顶
                ev_timer_heap_pop(&g_ev_timer_heap);
                // 设置actived为0，等待重启
                ATOM_STORE(&timer->actived, 0, MORDER_RELEASE);

                expired_batch[batch_count++] = timer;
            }
        }

        // 锁外提交任务，避免回调函数删除定时器导致内存踩踏
        for(int i = 0; i < batch_count; ++ i)
            thp_submit_task(ev_timer, expired_batch[i]->cb, expired_batch[i]->args);

        usleep(100 * 1000);
    }
}

ev_timer_t* ev_timer_create(const char *name, uint32_t timeout, ev_timer_cb_func cb, void *args)
{
    assert(name && timeout && cb);

    ev_timer_t *timer = mp_calloc(1, sizeof(ev_timer_t));
    timer->name = mp_strdup(name);
    timer->expired_time = ATOM_LOAD(&g_curr_ms, MORDER_ACQUIRE) + timeout;
    timer->timeout = timeout;
    timer->cb = cb;
    timer->args = args;

    return timer;
}

void ev_timer_destroy(ev_timer_t **timer)
{
    assert(timer && *timer);
    
    // 如果定时器还在堆中，从堆中移除timer
    if(ATOM_LOAD(&(*timer)->actived, MORDER_ACQUIRE))
    {
        ev_with_spinlock(&g_heap_spinlock)
            ev_timer_heap_del(&g_ev_timer_heap, *timer);
    }

    // 释放内存
    mp_free((*timer)->name, strlen((*timer)->name));
    mp_free(*timer, sizeof(ev_timer_t));
    *timer = NULL;
}

void ev_timer_start(ev_timer_t *timer)
{
    assert(timer);

    // 如果定时器还在堆中，从堆中移除timer
    if(ATOM_LOAD(&timer->actived, MORDER_ACQUIRE))
    {
        ev_with_spinlock(&g_heap_spinlock)
            ev_timer_heap_del(&g_ev_timer_heap, timer);
        timer->actived = 0;
    }

    // 设置actived 0->1
    unsigned char expected = 0;
    if(ATOM_CMP_XCHG_WEAK(&timer->actived, &expected, 1, MORDER_SEQ_SCT, MORDER_RELAXED))
    {
        ATOM_STORE(&timer->deleted, 0, MORDER_RELEASE);
        // 重新设置绝对超时时间
        timer->expired_time = ATOM_LOAD(&g_curr_ms, MORDER_ACQUIRE) + timer->timeout;
        ev_with_spinlock(&g_heap_spinlock)
            ev_timer_heap_add(&g_ev_timer_heap, timer); // 加入堆
    }
}

void ev_timer_stop(ev_timer_t *timer)
{
    assert(timer);
    // 设置deleted标记
    ATOM_STORE(&timer->deleted, 1, MORDER_RELEASE);
}

uint64_t ev_timer_remain(ev_timer_t *timer)
{
    assert(timer);

    ATOMIC_UINT8_T actived = ATOM_LOAD(&timer->actived, MORDER_ACQUIRE);
    ATOMIC_UINT8_T deleted = ATOM_LOAD(&timer->deleted, MORDER_ACQUIRE);
    
    // 已删除或者未运行的
    if(!actived || deleted)
        return 0;
    
    return timer->expired_time - ATOM_LOAD(&g_curr_ms, MORDER_ACQUIRE);
}

/**
 * @brief       hr_timer tfd readable event, cb
 * 
 * @param[in]   fd      - unused
 * @param[in]   args    - hr_timer
 * 
 * @note        高精度定时器到期时，ev loop调用，将回调传给线程池执行
 */
static void _ev_high_res_timer_el_cb(int fd, void *args)
{
    ev_high_res_timer_t *hr_timer = (ev_high_res_timer_t*)args;

    dbg("High res timer [%s] expired, put task into thread pool", hr_timer->name);

    thp_submit_task(ev_high_res_timer, hr_timer->cb, hr_timer->args);   // 投递到线程池
}

ev_high_res_timer_t* ev_high_res_timer_create(const char *name, uint64_t timeout, ev_timer_cb_func cb, void *args)
{
    assert(name && timeout && cb);

    ev_high_res_timer_t *hr_timer = mp_calloc(1, sizeof(ev_high_res_timer_t));
    hr_timer->name = mp_strdup(name);
    hr_timer->cb = cb;
    hr_timer->args = args;
    hr_timer->tv = timeout;

    // 创建Timer fd，非阻塞，fork-exec自动关闭
    hr_timer->tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if(hr_timer->tfd < 0)
    {
        dbg_error("create timer fd fail");
        goto error;
    }

    // 设置参数全0，相当于不激活
    struct itimerspec its = {0};
    if(timerfd_settime(hr_timer->tfd, 0, &its, NULL) < 0)
    {
        dbg_error("timer fd set fail");
        goto error;
    }

    // tfd注册到evloop
    //event_loop_register_file_event_timerfd(hr_timer->tfd, EL_FILE_EVENT_READABLE, _ev_high_res_timer_el_cb, hr_timer);
    // 加入epoll监听EPOLLIN
    struct epoll_event ev = {
        .events = EPOLLIN,
        .data.ptr = hr_timer,
    };
    if(0 != epoll_ctl(g_high_res_timer_epoll_fd, EPOLL_CTL_ADD, hr_timer->tfd, &ev))
        goto error;

    return hr_timer;

error:
    if(hr_timer && hr_timer->tfd >= 0)
        close(hr_timer->tfd);
    mp_free(hr_timer, sizeof(ev_high_res_timer_t));

    return NULL;
}

void ev_high_res_timer_start(ev_high_res_timer_t *hr_timer)
{
    assert(hr_timer && hr_timer->tfd != -1);

    // 设置参数，启动定时器
    struct itimerspec its = {
        .it_interval.tv_nsec = (hr_timer->tv % 1000) * 1000000L,
        .it_interval.tv_sec = hr_timer->tv / 1000,
        .it_value.tv_nsec = (hr_timer->tv % 1000) * 1000000L,
        .it_value.tv_sec = hr_timer->tv / 1000,
    };

    if(timerfd_settime(hr_timer->tfd, 0, &its, NULL) < 0)
    {
        char ctx[SYSLOG_ENTRY_CTX_SIZE] = {};
        snprintf(ctx, sizeof(ctx), "ev_hr_timer [%s] start fail", hr_timer->name);
        syslog_warn(SYSLOG_MODULE_TIMER, ctx);
    }

    return;
}

void ev_high_res_timer_stop(ev_high_res_timer_t *hr_timer)
{
    assert(hr_timer && hr_timer->tfd >= 0);

    // 设置时间0，相当于暂停
    struct itimerspec its = {0};
    if(timerfd_settime(hr_timer->tfd, 0, &its, NULL) < 0)
        dbg_error("stop timer %s fail", hr_timer->name);
}

static void* _ev_timer_high_res_epoll_main(void *args)
{
    prctl(PR_SET_NAME, "hrt_epoll_main");
    swdg_register("hrt_epoll_main", EV_HIGH_RES_EPOLL_WAIT * 3);

    ATOM_STORE(&g_high_res_timer_run, 1, MORDER_RELEASE);

    struct epoll_event events[EV_HIGH_RES_TIMER_COUNT_MAX] = {};

    while(ATOM_LOAD(&g_high_res_timer_run, MORDER_ACQUIRE))
    {
        swdg_feed();
        int nfds = epoll_wait(g_high_res_timer_epoll_fd, events, EV_HIGH_RES_TIMER_COUNT_MAX, EV_HIGH_RES_EPOLL_WAIT);     // 阻塞等待
        if(nfds < 0)
        {
            if(errno == EINTR)
                continue;
            perror("high_res epoll_wait");
            break;
        }

        // 处理就绪事件
        for(int i = 0; i < nfds; ++ i)
        {
            ev_high_res_timer_t *hr_timer = events[i].data.ptr;
            // 先把eventfd读取完毕
            eventfd_read_all(hr_timer->tfd);

            dbg("high res timer [%s] expired, put task into thp", hr_timer->name);

            thp_submit_task(ev_high_res_timer, hr_timer->cb, hr_timer->args);
        }
    }

    return NULL;
}

uint64_t low_res_cur_time_get()
{
    return ATOM_LOAD(&g_curr_ms, MORDER_ACQUIRE);
}

uint64_t running_time_get()
{
    uint64_t curr = mono_time_get();
    return curr - g_startup_ms;
}

void ev_timer_module_init(void)
{
    // 创建epoll_fd用于高精度定时器
    g_high_res_timer_epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    pfm_ensure_ret_void(g_high_res_timer_epoll_fd >= 0);

    // 更新当前低精度时钟
    uint64_t ms = mono_time_get();
    ATOM_STORE(&g_curr_ms, ms, MORDER_RELEASE);
    // 设置启动时间
    g_startup_ms = ms;

    // 低精度定时器执行回调的线程池
    thp_register(ev_timer);
    thp_run(ev_timer);

    // 高精度定时器专用线程池
    thp_register(ev_high_res_timer);
    thp_run(ev_high_res_timer);

    ev_timer_heap_init(&g_ev_timer_heap);

    ev_thd_register(low_res_clock);
    ev_thd_run(low_res_clock);
    ev_thd_wake(low_res_clock);     // 唤醒一次

    // 高精度定时器管理线程
    pthread_create(&g_high_res_timer_epoll_tid, NULL, _ev_timer_high_res_epoll_main, NULL);

    dbg_major("ev timer module init ok");
}