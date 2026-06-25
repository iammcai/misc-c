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
#include "plat/atom.h"
#include "event/ev_timer.h"
#include "event/ev_thread.h"
#include "event/ev_lock.h"
#include "plat/debug.h"
#include "mp/mp.h"
#include "thp/thp.h"
#include "event/ev_loop.h"

/* ========================================================================== */
/*                            Macro Definitions                               */
/* ========================================================================== */

// 高精度定时器监听数量
#define EV_HIGH_RES_TIMER_COUNT_MAX     (64)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 定时器定义
struct ev_timer_s{
    uint64_t expired_time;      // 绝对超时时间
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

static attr_force_inline uint64_t _mono_time_get()
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
 * @brief       ctor hook for low_res_clock
 * 
 * @note        低精度线程ctor钩子，用来创建线程池
 */
static attr_force_inline void _low_res_clock_init();

/**
 * @brief       ctor init ev timer
 * 
 * @note        （1）启动低精度定时器（2）初始化线程池
 */
static void ev_timer_init(void) attr_ctor(CTOR_PRIO_LOW_LOW);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 低精度时钟更新线程，每100ms唤醒一次
declare_ev_thd(low_res_clock, _low_res_clock_work, NULL, 100)
// 注册ctor钩子，用于初始化线程池使用的内存池
declare_ev_thd_ctor(low_res_clock, _low_res_clock_init)
// 当前时间，ms表示
static ATOMIC_UINT64_T g_curr_ms = 0;
// 最小堆，管理定时器
static ev_timer_heap_head_t g_ev_timer_heap = {};
// 自旋锁，用来保护堆
static ev_spinlock_t g_heap_spinlock = EV_SPINLOCK_INITIALIZER;
// 线程池，用于处理定时任务，由low_res_clock管理
static thp_t _thp_ev_timer = {};

// 用于高精度定时器的线程池
static thp_t _thp_ev_high_res_timer = {.shutdown = 1,};     // 初始shutdown

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

// 定义堆操作
declare_heap(ev_timer, ev_timer, ev_timer_t, item, 1, 1024, _ev_timer_cmp)

static inline void _low_res_clock_init()
{
    thp_init(&_thp_ev_timer, "ev_timer", THREAD_POOL_TH_COUNT_MAX);
    // 启动
    thp_run(ev_timer);
}

static void _low_res_clock_work(void *args)
{
    uint64_t ms = _mono_time_get();
    ATOM_STORE(&g_curr_ms, ms, MORDER_RELEASE);

    ev_with_spinlock(&g_heap_spinlock)
    {
        ev_timer_t *timer;

        while(timer = ev_timer_heap_top(&g_ev_timer_heap))
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

            dbg("timer expired, cur %lu", ms);

            // 线程池异步执行任务
            thp_submit_task(ev_timer, timer->cb, timer->args);

            // 定时器移出堆顶
            ev_timer_heap_pop(&g_ev_timer_heap);
            // 设置actived为0，等待重启
            ATOM_STORE(&timer->actived, 0, MORDER_RELEASE);
        }
    }
}

ev_timer_t* ev_timer_create(uint32_t timeout, ev_timer_cb_func cb, void *args)
{
    assert(timeout && cb);

    ev_timer_t *timer = mp_calloc(1, sizeof(ev_timer_t));
    timer->expired_time = ATOM_LOAD(&g_curr_ms, MORDER_ACQUIRE) + timeout;
    timer->cb = cb;
    timer->args = args;

    return timer;
}

void ev_timer_start(ev_timer_t *timer)
{
    assert(timer);

    // 设置actived 0->1
    unsigned char expected = 0;
    if(ATOM_CMP_XCHG_WEAK(&timer->actived, &expected, 1, MORDER_SEQ_SCT, MORDER_RELAXED))
    {
        ATOM_STORE(&timer->deleted, 0, MORDER_RELEASE);
        ev_with_spinlock(&g_heap_spinlock)
            ev_timer_heap_add(&g_ev_timer_heap, timer); // 加入堆

        dbg("start timer %dms", timer->expired_time);
    }
}

void ev_timer_stop(ev_timer_t *timer)
{
    assert(timer);
    // 设置deleted标记
    ATOM_STORE(&timer->deleted, 1, MORDER_RELEASE);
}

/**
 * @brief       hr_timer tfd readable event, cb
 * 
 * @param[in]   args    - hr_timer
 * 
 * @note        高精度定时器到期时，ev loop调用，将回调传给线程池执行
 */
static void _ev_high_res_timer_el_cb(void *args)
{
    ev_high_res_timer_t *hr_timer = (ev_high_res_timer_t*)args;

    // 线程池未启动的话，初始化启动一下。这是因为thp包含mp，这个函数由ev loop调用，那边没有初始化mp相关
    if(!_thp_is_run(&_thp_ev_high_res_timer))
    {
        thp_init(&_thp_ev_high_res_timer, "ev_high_res_timer", THREAD_POOL_TH_COUNT_MAX);
        thp_run(ev_high_res_timer);
    }

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
    event_loop_register_file_event_timerfd(hr_timer->tfd, EL_FILE_EVENT_READABLE, _ev_high_res_timer_el_cb, hr_timer);

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
        .it_interval = 0,   // 单次启动
        .it_value.tv_nsec = (hr_timer->tv % 1000) * 1000000L,
        .it_value.tv_sec = hr_timer->tv / 1000,
    };

    if(timerfd_settime(hr_timer->tfd, 0, &its, NULL) < 0)
        dbg_error("start timer %s fail", hr_timer->name);

    dbg("timer %s start", hr_timer->name);

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

static void ev_timer_init(void)
{
    // 更新当前时钟
    uint64_t ms = _mono_time_get();
    ATOM_STORE(&g_curr_ms, ms, MORDER_RELEASE);

    ev_timer_heap_init(&g_ev_timer_heap);   // 初始化堆
    ev_thd_run(low_res_clock);      // 启动低精度时钟线程
}