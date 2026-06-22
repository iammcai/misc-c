/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    ev_timer.c
 * @brief   用户态定时器实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-17
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-17 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <time.h>
#include "plat/atom.h"
#include "event/ev_timer.h"
#include "event/ev_thread.h"
#include "event/ev_lock.h"
#include "plat/debug.h"
#include "mp/mp.h"
#include "thp/thp.h"

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

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

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
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ms = ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL;
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

static void ev_timer_init(void)
{
    // 更新当前时钟
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ms = ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL;
    ATOM_STORE(&g_curr_ms, ms, MORDER_RELEASE);

    ev_timer_heap_init(&g_ev_timer_heap);   // 初始化堆
    ev_thd_run(low_res_clock);      // 启动低精度时钟线程
}