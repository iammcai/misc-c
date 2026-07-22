/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    ev_thread.c
 * @brief   事件线程实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-05-29
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-05-29 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <unistd.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/eventfd.h>
#include "event/ev_thread.h"
#include "plat/atom.h"
#include "event/ev_loop.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 安全关闭文件描述符
#define ev_fd_close(fd) do {    \
    if(fd >= 0) {    \
        close(fd);  \
        fd = -1;    \
    }   \
}while(0);  \
/* ev_fd_close end */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

// 定义存储钩子函数的链表函数
declare_list(ev_thd_hook, ev_thd_hook, ev_thd_hook_t, item)

/**
 * @brief       cmp func between evthd attrs
 * 
 * @param[in]   attr1   - attr 1
 * @param[in]   attr2   - attr 2
 * 
 * @retval      cmp result
 */
static attr_pure_inline int ev_thd_attr_cmp(ev_thd_attr_t *attr1, ev_thd_attr_t *attr2)
{
    assert(attr1 && attr1->name && attr2 && attr2->name);
    return strcmp(attr1->name, attr2->name);
}

/**
 * @brief       hash func of evthd attr
 * 
 * @param[in]   attr    - attr
 * 
 * @retval      hash result
 */
static attr_pure_inline unsigned int ev_thd_attr_hash(ev_thd_attr_t *attr)
{
    assert(attr && attr->name);
    return type_hash_jhash(attr->name, strlen(attr->name), 0);
}

/**
 * @brief       walk ev thd hooks
 * 
 * @param[in]   attr    - ev thd attr
 * @param[in]   type    - hook type
 * 
 * @note        遍历执行注册的钩子函数
 */
static attr_force_inline void ev_thd_hook_walk(ev_thd_attr_t *attr, ev_thd_hook_type_e type)
{
    ev_thd_hook_t *hook = NULL;
    ev_thd_hook_list_head_t *head = NULL;

    switch(type)
    {
        case EV_THD_HOOK_TYPE_CTOR:
            head = &attr->ctors;
            break;
        case EV_THD_HOOK_TYPE_PREWORK:
            head = &attr->preworks;
            break;
        case EV_THD_HOOK_TYPE_POSTWORK:
            head = &attr->postworks;
            break;
        case EV_THD_HOOK_TYPE_DTOR:
            head = &attr->dtors;
            break;
        default:
            return;
    }

    hook = ev_thd_hook_list_first(head);
    while(hook)
    {
        hook->hook_func();  // 执行
        hook = ev_thd_hook_list_next(hook);
    }
}

/**
 * @brief       timer cb for ev thd
 * 
 * @param[in]   ev_thd_attr
 * 
 * @note        ev_thd 自唤醒定时器的回调函数
 */
static attr_force_inline void _ev_thd_timer_cb(void *args)
{
    ev_thd_attr_t *attr = (ev_thd_attr_t*)args;

    ev_sem_post(&attr->sem);    // 写sem唤醒工作线程
    // 重新启动定时器
    ev_high_res_timer_start(attr->timer);
}

/**
 * @brief       eventfd cb for ev thd
 * 
 * @param[in]   fd
 * @param[in]   ev_thd_attr
 * 
 * @note        ev_thd eventfd可写回调
 */
static attr_force_inline void _ev_thd_eventfd_cb(int fd, void *args)
{
    ev_thd_attr_t *attr = (ev_thd_attr_t*)args;
    ev_sem_post(&attr->sem);    // 写sem唤醒工作线程
}

/**
 * @brief       init event components
 * 
 * @param[in]   attr    - ev thd attr
 * 
 * @note        创建一个event fd内核计数器用来产生可写事件，监听他来唤醒线程；创建定时器自唤醒
 */
static attr_force_inline void ev_thd_event_init(ev_thd_attr_t *attr);

/**
 * @brief       sem wait
 * 
 * @param[in]   attr    - ev thd attr
 * 
 * @note        通过sem阻塞等待eventfd可写事件，由ev_loop通知
 */
static attr_force_inline void ev_thd_event_wait(ev_thd_attr_t *attr)
{
    ev_sem_wait(&attr->sem);
}

/**
 * @brief       wake by write event fd
 * 
 * @param[in]   attr    - ev thd attr
 * 
 * @note        主动唤醒，如果设置了自唤醒，那么先停止定时器，唤醒后再重新启动
 */
static attr_force_inline void ev_thd_event_wake(ev_thd_attr_t *attr)
{
    assert(attr);
    
    // 如果有定时器，那么先停止运行
    if(attr->timeout && attr->timer)
        ev_high_res_timer_stop(attr->timer);

    eventfd_write_one(attr->event_fd);  // 写eventfd，evloop会得知

    // 如果有定时器，那么重新启动
    if(attr->timeout && attr->timer)
        ev_high_res_timer_start(attr->timer);
}

/**
 * @brief       event fini
 * 
 * @param[in]   attr    - ev thd attr
 * 
 * @note        回收eventfd，timerfd资源等
 */
static attr_force_inline void ev_thd_event_fini(ev_thd_attr_t *attr)
{
    // 移除eventfd在evloop中的注册
    event_loop_deregister_file_event(attr->event_fd);

    // 取消定时器，但是不销毁
    if(attr->timeout && attr->timer)
        ev_high_res_timer_stop(attr->timer);
}

/**
 * @brief       event thread routine
 * 
 * @param[in]   args    - attr
 * 
 * @retval      NULL
 * 
 * @note        posix线程入口函数
 */
static void* ev_thd_routine(void *args);

/**
 * @brief       create thread
 * 
 * @param[in]   attr    - ev thd attr
 * 
 * @retval      thread ID
 */
static attr_force_inline pthread_t ev_thd_create(ev_thd_attr_t *attr)
{
    pthread_t tid = 0;
    int ret = 0;

    ret = pthread_create(&tid, NULL, ev_thd_routine, attr);     // posix创建线程
    assert(!ret);

    return tid;
}

/**
 * @brief       join thread
 * 
 * @param[in]   attr    - ev thd attr
 */
static attr_force_inline void ev_thd_join(ev_thd_attr_t *attr)
{
    assert(attr);
    pthread_join(attr->tid, NULL);
}

/**
 * @brief       clear ev thd attr
 * 
 * @param[in]   attr    - ev thd attr
 * 
 * @note        对于cancel线程时没有重置的一些属性，这里集中处理
 */
static attr_force_inline void ev_thd_attr_clear(ev_thd_attr_t *attr)
{
    assert(attr);

    ATOM_STORE(&attr->tid, 0, MORDER_RELAXED);      // 删除tid记录
}

// 定义存储线程属性的哈希表的函数
declare_hash(ev_thd_attr, ev_thd_attr, ev_thd_attr_t, item, 1, 31, ev_thd_attr_cmp, ev_thd_attr_hash)

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

// 全局哈希表，存储所有事件线程属性
static ev_thd_attr_hash_head_t g_ev_thd_attr_hash_head = {};

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void ev_thd_module_init()
{
    ev_thd_attr_hash_init(&g_ev_thd_attr_hash_head);

    dbg_major("event thread init ok");
}

void ev_thd_attr_init(ev_thd_attr_t *attr)
{
    ev_sem_init(&attr->sem);        // 初始化信号量
    if(attr->timeout)               // 如果设置了超时唤醒，创建定时器
        attr->timer = ev_high_res_timer_create(attr->name, attr->timeout, _ev_thd_timer_cb, attr);

    // 创建内核计数器，后续用于唤醒
    attr->event_fd = eventfd(0, O_CLOEXEC | O_NONBLOCK);
    assert(attr->event_fd > 0);

    ev_thd_attr_hash_add(&g_ev_thd_attr_hash_head, attr);   // 加入全局哈希表
    // 初始化各个钩子链表头部
    ev_thd_hook_list_init(&attr->ctors);
    ev_thd_hook_list_init(&attr->preworks);
    ev_thd_hook_list_init(&attr->postworks);
    ev_thd_hook_list_init(&attr->dtors);
}

static inline void ev_thd_event_init(ev_thd_attr_t *attr)
{
    // eventfd注册到event_loop进行监听
    //dbg_always("attr->event_fd %d", attr->event_fd);
    event_loop_register_file_event_eventfd(attr->event_fd, EL_FILE_EVENT_READABLE, _ev_thd_eventfd_cb, attr);

    // 启动定时器
    if(attr->timeout && attr->timer)
        ev_high_res_timer_start(attr->timer);
}

static void* ev_thd_routine(void *args)
{
    assert(args);

    ev_thd_attr_t *attr = (ev_thd_attr_t*)args;

    prctl(PR_SET_NAME, attr->name);     // 设置线程名，方便调试

    // 运行ctor钩子
    ev_thd_hook_walk(attr, EV_THD_HOOK_TYPE_CTOR);

    // 初始化event_fd, timer等组件
    ev_thd_event_init(attr);

    // 设置working标志
    ATOM_STORE(&attr->working, 1, MORDER_RELEASE);

    // 进入循环等待-工作，检查run标志，1说明线程还没结束
    while(ATOM_LOAD(&attr->run, MORDER_ACQUIRE))
    {
        ev_thd_event_wait(attr);    // 进入等待，这里阻塞

        // 再次检查run标志，防止已经cancel
        if(ATOM_LOAD(&attr->run, MORDER_ACQUIRE))
        {
            // 运行prework钩子
            ev_thd_hook_walk(attr, EV_THD_HOOK_TYPE_PREWORK);

            // real work
            attr->work(attr->args);

            // postwork钩子
            ev_thd_hook_walk(attr, EV_THD_HOOK_TYPE_POSTWORK);
        }
    }

    /* 走到此说明cancel */

    // 设置working标志
    ATOM_STORE(&attr->working, 0, MORDER_RELEASE);

    // 清理eventfd资源
    ev_thd_event_fini(attr);

    // 执行dtor钩子
    ev_thd_hook_walk(attr, EV_THD_HOOK_TYPE_DTOR);

    return NULL;
}

void _ev_thd_run(ev_thd_attr_t *attr)
{
    char run = 0;

    // 尝试将attr->run 0->1
    while(!ATOM_CMP_XCHG_WEAK(&attr->run, &run, 1, MORDER_ACQ_REL, MORDER_RELAXED))
    {
        if(run)         // 说明attr->run原本就是1
            return;
    }

    assert(attr->work);     // 确认有指定工作函数，否则启动无意义

    attr->tid = ev_thd_create(attr);   // 创建线程运行，返回tid

    while(!ATOM_LOAD(&attr->working, MORDER_ACQUIRE))    // 等待子线程设置了working标志，再结束
        usleep(1000);
}

void _ev_thd_wake(ev_thd_attr_t *attr)
{
    assert(attr);

    // 检查线程是否在运行
    if(!ATOM_LOAD(&attr->working, MORDER_ACQUIRE))
        return;

    // 主动唤醒
    ev_thd_event_wake(attr);
}

void _ev_thd_cancel(ev_thd_attr_t *attr)
{
    assert(attr);

    char expected_run = 1;
    // 设置run标志 1->0
    while(!ATOM_CMP_XCHG_WEAK(&attr->run, &expected_run, 0, MORDER_ACQ_REL, MORDER_RELAXED))
        if(expected_run == 0)   // 本来就是0，那么不需要重复cancel
            return;

    // 唤醒线程，routine检查run标志0，后续结束
    _ev_thd_wake(attr);

    // 等待线程结束
    ev_thd_join(attr);

    // 清空attr属性，不影响重新启动
    ev_thd_attr_clear(attr);
}

void ev_thd_hook_reg(ev_thd_attr_t *attr, ev_thd_hook_t *hook, ev_thd_hook_type_e type)
{
    assert(attr && hook);

    ev_thd_hook_list_head_t *head = NULL;
    switch(type)
    {
        case EV_THD_HOOK_TYPE_CTOR:
            head = &attr->ctors;
            break;
        case EV_THD_HOOK_TYPE_PREWORK:
            head = &attr->preworks;
            break;
        case EV_THD_HOOK_TYPE_POSTWORK:
            head = &attr->postworks;
            break;
        case EV_THD_HOOK_TYPE_DTOR:
            head = &attr->dtors;
            break;
        default:
            return;
    }

    ev_thd_hook_list_add_tail(head, hook);
}