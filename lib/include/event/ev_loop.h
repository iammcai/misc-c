/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    ev_loop.h
 * @brief   reactor事件驱动框架头文件
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

#ifndef __EV_LOOP_H__
#define __EV_LOOP_H__

 /* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <pthread.h>
#include <pthread.h>
#include "plat/compiler.h"
#include "event/ev_lock.h"
#include "type/type_list.h"
#include "sys/eventfd.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 声明事件循环管理结构
struct el_s;

/**
 * @brief       file事件回调函数签名
 * 
 * @param[in]   fd          - file desc
 * @param[in]   client_args - args
 * 
 * @note        必须轻量化，避免阻塞过久
*/
typedef void (*el_file_event_cb)(void *client_args);

// file mask 枚举定义
typedef enum{
    EL_FILE_EVENT_NONE = 0,             // 未注册，用于安全覆盖
    EL_FILE_EVENT_READABLE = 1 << 0,    // 注册了可读事件
    EL_FILE_EVENT_WRITEABLE = 1 << 1,   // 注册了可写事件
}el_file_event_mask_e;

// file事件类型枚举
typedef enum{
    EL_FILE_EVENT_TYPE_NORMAL = 0,      // 普通
    EL_FILE_EVENT_TYPE_EVENTFD,         // eventfd，内核计数器
    EL_FILE_EVENT_TYPE_TIMERFD,         // timerfd，内核高精度定时器

    EL_FILE_EVENT_TYPE_CNT,             // 用于计数
}el_file_event_type_e;

// file事件结构定义
typedef struct{
    unsigned char mask;         // 掩码，表示监听读写事件，见el_file_event_mask_e
    el_file_event_type_e type;  // fd类型
    el_file_event_cb read_cb;   // 可读事件回调
    el_file_event_cb write_cb;  // 可写事件回调
    void *args;                 // 回调参数
}el_file_event_t;

// file快照结构定义
typedef struct{
    int fd;                 // 就绪fd
    unsigned char mask;     // 事件类型mask
}el_file_event_fired_t;

// 预定义注册file事件的链表结构
pre_declare_list(el_fe_reg)
// 注册file事件的链表节点定义
typedef struct{
    int fd;
    el_file_event_t el_file_event;  // ctx
    el_fe_reg_list_item_t item;     // list item
}el_fe_reg_item_t;

// 事件循环管理结构定义
typedef struct el_s{
    int epoll_fd;               // epoll fd

    el_file_event_t *file_events;           // 存储file事件的数组，以fd为下标。为了性能，不使用哈希表
    el_file_event_fired_t *file_fireds;     // file_events运行时快照，防止多个fd互相操作破坏结构

    el_fe_reg_list_head_t file_events_reg_list;     // 用于运行时修改事件的链表
    ev_spinlock_t file_events_reg_spinlock;         // 保护file_events_reg_list
    int file_events_reg_event_fd;                   // eventfd，用于唤醒处理注册

    pthread_t ev_loop_th;       // 进行loop的线程id
    #define EV_LOOP_FILE_EVENT_COUNT_MAX    (512)               // 支持的file事件最大数量
    struct epoll_event events[EV_LOOP_FILE_EVENT_COUNT_MAX];    // 事件缓冲区
}el_t;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/**
 * 外部使用，注册普通file事件到event loop
 */
#define event_loop_register_file_event(fd, mask, cb_func, args) \
    _el_reg_fe(fd, EL_FILE_EVENT_TYPE_NORMAL, mask, cb_func, args); \
/* event_loop_register_file_event end */

/**
 * 外部使用，注册timerfd事件到event loop
 */
#define event_loop_register_file_event_timerfd(fd, mask, cb_func, args) \
    _el_reg_fe(fd, EL_FILE_EVENT_TYPE_TIMERFD, mask, cb_func, args); \
/* event_loop_register_file_event_timerfd end */

/**
 * 外部使用，注册eventfd事件到event loop
 */
#define event_loop_register_file_event_eventfd(fd, mask, cb_func, args) \
    _el_reg_fe(fd, EL_FILE_EVENT_TYPE_EVENTFD, mask, cb_func, args); \
/* event_loop_register_file_event_eventfd end */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       注册file事件到event loop
 * 
 * @param[in]   fd      - file desc
 * @param[in]   type    - file event type
 * @param[in]   mask    - mask, read - write
 * @param[in]   cb_func - callback
 * @param[in]   args    - cb args
 */
extern void _el_reg_fe(int fd, el_file_event_type_e type, unsigned char mask, el_file_event_cb cb_func, void *args);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

/**
 * @brief       eventfd write once
 * 
 * @param[in]   eventfd - event fd
 */
static attr_force_inline void eventfd_write_one(int eventfd)
{
    uint64_t val = 1;
    write(eventfd, &val, sizeof(val));
}

/**
 * @brief       eventfd read whole buffer
 * 
 * @param[in]   eventfd - event fd
 */
static attr_force_inline void eventfd_read_all(int eventfd)
{
    // 读完eventfd缓冲
    uint64_t val = 0;
    while(1)
    {
        ssize_t n = read(eventfd, &val, sizeof(val));
        if(n > 0)
            continue;
        else if(n < 0 && errno == EINTR)
            continue;
        break;
    }
}

/**
 * @brief       timerfd read whole buffer
 * 
 * @param[in]   timerfd - timer fd
 * 
 * @note        和eventfd一样，直接复用
 */
static attr_force_inline void timerfd_read_all(int timerfd)
{
    eventfd_read_all(timerfd);
}

#endif
/* __EV_LOOP_H__ end*/