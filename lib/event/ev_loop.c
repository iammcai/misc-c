/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    ev_loop.c
 * @brief   reactor事件驱动框架实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-24
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-24 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <sys/epoll.h>
#include <unistd.h>
#include <sys/errno.h>
#include "event/ev_loop.h"
#include "mp/mp.h"
#include "plat/debug.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// epoll永久阻塞等待时间
#define EV_LOOP_WAIT_4EVER      (-1)

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 全局事件循环管理结构
static el_t g_event_loop = {};

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

// 定义file事件动态注册链表操作
declare_list(el_fe_reg, el_fe_reg, el_fe_reg_item_t, item)

/**
 * @brief       main func of event loop
 * 
 * @note        真正执行事件分发的线程工作函数
 */
static void* _el_main(void *args);

/**
 * @brief       epoll wait for el
 * 
 * @retval      ready event nums
 * 
 * @note        内部使用，包含调用epoll_wait死等，以及设置就绪快照
 */
static attr_force_inline int _el_epoll_wait(void)
{
    // 阻塞等待，直到事件产生
    int nums = epoll_wait(g_event_loop.epoll_fd, g_event_loop.events, EV_LOOP_FILE_EVENT_COUNT_MAX, EV_LOOP_WAIT_4EVER);

    // 复制快照
    int i = 0;
    for(; i < nums; ++ i)
    {
        unsigned mask = EL_FILE_EVENT_NONE;
        struct epoll_event *ev = g_event_loop.events + i;
        if(ev->events & EPOLLIN)
            mask |= EL_FILE_EVENT_READABLE;
        if(ev->events & EPOLLOUT)
            mask |= EL_FILE_EVENT_WRITEABLE;

        g_event_loop.file_fireds[i].fd = ev->data.fd;   // 设置fd
        g_event_loop.file_fireds[i].mask = mask;        // 设置事件mask
    }

    return nums;
}

/**
 * @brief       处理动态注册file事件
 * 
 * @note        读取注册链表进行写入
 */
static void _el_reg_fe_handle()
{
    // 遍历注册队列，取出来
    el_fe_reg_item_t *it = NULL;
    ev_with_spinlock(&g_event_loop.file_events_reg_spinlock)
        it = el_fe_reg_list_pop(&g_event_loop.file_events_reg_list);
    while(it)
    {
        int index = it->fd;     // 以fd作为数组索引
        if(!(index >=0 && index < EV_LOOP_FILE_EVENT_COUNT_MAX))    // 超出索引处理
        {
            dbg_error("fd %d out of range", index);
            mp_free(it, sizeof(el_fe_reg_item_t));
            continue;
        }
        // file_events数组中对应槽位
        el_file_event_t *fe = g_event_loop.file_events + index;
        if(EL_FILE_EVENT_NONE != it->el_file_event.mask)
        {
            // 获取原来的mask，可能原来注册了read，要补充注册write，直接EPOLL_CTL_ADD会错误呢，必须MOD
            int epoll_op = fe->mask == EL_FILE_EVENT_NONE ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
            unsigned char mask = it->el_file_event.mask | fe->mask;

            struct epoll_event ev = {.data.fd = it->fd, .events = EPOLLET};
            if(mask & EL_FILE_EVENT_READABLE)
                ev.events |= EPOLLIN;
            if(mask & EL_FILE_EVENT_WRITEABLE)
                ev.events |= EPOLLOUT;
            epoll_ctl(g_event_loop.epoll_fd, epoll_op, it->fd, &ev);

            // 写入到全局file_event array，需要增量修改
            if(it->el_file_event.mask & EL_FILE_EVENT_READABLE)
                fe->read_cb = it->el_file_event.read_cb;
            if(it->el_file_event.mask & EL_FILE_EVENT_WRITEABLE)
                fe->write_cb = it->el_file_event.write_cb;
            if(fe->args == NULL || it->el_file_event.args != NULL)
                fe->args = it->el_file_event.args;
            fe->mask = mask;

            //dbg_always("register fd %d into loop done", it->fd);
        }
        else        // 注销
        {
            // 从epoll移除
            epoll_ctl(g_event_loop.epoll_fd, EPOLL_CTL_DEL, it->fd, NULL);
            fe->mask = EL_FILE_EVENT_NONE;      // 清除数组标记
            //dbg_always("deregister fd %d in loop", it->fd);
        }
        // 释放内存
        mp_free(it, sizeof(el_fe_reg_item_t));

        // 获取下一个
        ev_with_spinlock(&g_event_loop.file_events_reg_spinlock)
            it = el_fe_reg_list_pop(&g_event_loop.file_events_reg_list);
    }
}

/**
 * @brief       event loop ctor init
 * 
 * @note        构造初始化，主要是g_event_loop变量初始化
 */
static void _ev_loop_early_init() attr_ctor(CTOR_PRIO_HIGH);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void _el_reg_fe(int fd, el_file_event_type_e type, unsigned char mask, el_file_event_cb cb_func, void *args)
{
    pfm_ensure_ret_void(fd >= 0);

    // 申请内存，注册事件较少，直接动态申请
    el_fe_reg_item_t *it = mp_calloc(1, sizeof(el_fe_reg_item_t));
    it->fd = fd;
    it->el_file_event.type = type;
    it->el_file_event.mask = mask;
    it->el_file_event.args = args;
    if(mask & EL_FILE_EVENT_READABLE)
        it->el_file_event.read_cb = cb_func;
    if(mask & EL_FILE_EVENT_WRITEABLE)
        it->el_file_event.write_cb = cb_func;

    // 加到链表中
    ev_with_spinlock(&g_event_loop.file_events_reg_spinlock)
        el_fe_reg_list_add_tail(&g_event_loop.file_events_reg_list, it);

    dbg_major("call register fd %d into event loop", fd);

    // 写eventfd唤醒epoll进行注册
    uint64_t val = 1;
    pfm_ensure_ret_void(sizeof(val) == write(g_event_loop.file_events_reg_event_fd, &val, sizeof(val)));
}

static void* _el_main(void *args)
{
    while(1)    // 事件循环检测永不终止
    {
        // 等待事件
        int ready = _el_epoll_wait();
        // 处理被信号打断的场合
        if(ready < 0)
            if(errno == EINTR)
                continue;
            else
            {
                dbg_error("_el_epoll_wait fatal error: %s", strerror(errno));
                break;
            }

        // 进行事件分发，以fired数组为准
        //dbg_always("epoll wait return, ready num %d", ready);
        int i = 0;
        for(; i < ready; ++ i)
        {
            int fd = g_event_loop.file_fireds[i].fd;
            // 检查类型，看是否需要这里消耗掉可读
            el_file_event_t *fe = g_event_loop.file_events + fd;
            if(fe->type == EL_FILE_EVENT_TYPE_EVENTFD)
                eventfd_read_all(fd);
            else if(fe->type == EL_FILE_EVENT_TYPE_TIMERFD)
                timerfd_read_all(fd);

            if(fd == g_event_loop.file_events_reg_event_fd)     // 注册新的事件
                _el_reg_fe_handle();
            else if(fd >= 0 && fd < EV_LOOP_FILE_EVENT_COUNT_MAX)   // 分发事件
            {
                //dbg_always("distribute fd %d", fd);
                unsigned char fired_mask = g_event_loop.file_fireds[i].mask;
                if((fired_mask & fe->mask & EL_FILE_EVENT_READABLE) && fe->read_cb)
                    fe->read_cb(fd, fe->args);
                if((fired_mask & fe->mask & EL_FILE_EVENT_WRITEABLE) && fe->write_cb)
                    fe->write_cb(fd, fe->args);
            }
        }
    }

    return NULL;
}

static void _ev_loop_early_init()
{
    g_event_loop.epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    // 申请存储file事件的数组空间
    g_event_loop.file_events = mp_calloc(EV_LOOP_FILE_EVENT_COUNT_MAX, sizeof(el_file_event_t));
    g_event_loop.file_fireds = mp_calloc(EV_LOOP_FILE_EVENT_COUNT_MAX, sizeof(el_file_event_fired_t));

    // file事件动态注册链表初始化
    el_fe_reg_list_init(&g_event_loop.file_events_reg_list);
    ev_spinlock_init(&g_event_loop.file_events_reg_spinlock);
    g_event_loop.file_events_reg_event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);     // 创建内核计数器，初始值0
    // 把event_fd注册到epoll
    struct epoll_event ev = {
        .events = EPOLLIN | EPOLLET,
        .data.fd = g_event_loop.file_events_reg_event_fd,
    };
    pfm_ensure_ret_void(0 == epoll_ctl(g_event_loop.epoll_fd, EPOLL_CTL_ADD, g_event_loop.file_events_reg_event_fd, &ev));

    // 创建子线程，执行event loop
    pfm_ensure_ret_void(0 == pthread_create(&g_event_loop.ev_loop_th, NULL, _el_main, NULL));

    dbg_major("event loop init done");
}