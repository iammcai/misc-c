/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    ev_thread.h
 * @brief   事件线程头文件
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

#ifndef __EV_THREAD_H__
#define __EV_THREAD_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <pthread.h>
#include "plat/compiler.h"
#include "type/type_list.h"
#include "type/type_hash.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 预定义存储线程属性的哈希表结构
pre_declare_hash(ev_thd_attr)
// 预定义存储钩子函数的链表结构
pre_declare_list(ev_thd_hook)

// 线程工作函数定义
typedef void (*ev_thd_work_func)(void *args);
// 钩子函数定义
typedef void (*ev_thd_hook_func)(void);

// 线程属性定义，通过哈希表存储起来
typedef struct{
    const char *name;       // 属性名
    pthread_t tid;          // 线程ID
    int epoll_fd;           // epoll fd
    int event_fd;           // 用于唤醒线程的event fd

    char run;           // 启动标志
    char working;       // 工作标志

    ev_thd_hook_list_head_t ctors;      // ctor函数链表
    ev_thd_hook_list_head_t preworks;   // prework函数链表
    ev_thd_work_func work;              // 工作函数
    void *args;                         // 工作函数入参
    ev_thd_hook_list_head_t postworks;  // postwork函数链表
    ev_thd_hook_list_head_t dtors;      // dtor函数链表

    ev_thd_attr_hash_item_t item;   // 哈希表item
}ev_thd_attr_t;

// hook函数定义，通过链表存储起来
typedef struct{
    ev_thd_hook_func hook_func;     // 钩子函数
    ev_thd_hook_list_item_t item;   // 链表Item
}ev_thd_hook_t;

// 钩子函数类型
typedef enum{
    EV_THD_HOOK_TYPE_CTOR = 0,
    EV_THD_HOOK_TYPE_PREWORK,
    EV_THD_HOOK_TYPE_POSTWORK,
    EV_THD_HOOK_TYPE_DTOR,
}ev_thd_hook_type_e;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/**
 * 外部使用，定义一个事件驱动线程
 * 1. 定义事件线程属性，构造加入到哈希表中
 */
#define declare_ev_thd(_name, _wf, _wa) \
static ev_thd_attr_t ev_thd_attr_ ## _name = {  \
    .name = #_name, \
    .work = _wf,    \
    .args = _wa,    \
    .tid = 0,   \
    .epoll_fd = -1, \
    .event_fd = -1, \
    .run = 0,   \
    .working = 0,   \
};  \
static void ev_thd_attr_ ## _name ## _init() attr_ctor(CTOR_PRIO_MID);  \
static void ev_thd_attr_ ## _name ## _init()    \
{   \
    ev_thd_attr_init(&ev_thd_attr_ ## _name); \
}   \
/* declare_ev_thd end */

/**
 * 外部使用，启动一个线程，随后等待唤醒
 */
#define ev_thd_run(name)    \
    _ev_thd_run(&ev_thd_attr_ ## name); \
/* ev_thd_run end */

/**
 * 外部使用，唤醒一个线程
 */
#define ev_thd_wake(name)   \
    _ev_thd_wake(&ev_thd_attr_ ## name);    \
/* ev_thd_wake end */

/**
 * 外部使用，结束一个线程
 */
#define ev_thd_cancel(name) \
    _ev_thd_cancel(&ev_thd_attr_ ## name);
/* ev_thd_cancel end */

/**
 * 外部使用，注册ctor钩子
 */
#define declare_ev_thd_ctor(name, _hook_func)   \
static ev_thd_hook_t ev_thd_ctor_hook_ ## name ## _ ##_hook_func = { .hook_func = _hook_func };   \
static void ev_thd_ctor_hook_ ## name ## _ ##_hook_func ## _reg() attr_ctor(CTOR_PRIO_LOW);   \
static void ev_thd_ctor_hook_ ## name ## _ ##_hook_func ## _reg() \
{   \
    ev_thd_hook_reg(&ev_thd_attr_ ## name, &ev_thd_ctor_hook_ ## name ## _ ##_hook_func, EV_THD_HOOK_TYPE_CTOR); \
}   \
/* ev_thd_hook_ctor_reg */

/**
 * 外部使用，注册prework钩子
 */
#define declare_ev_thd_prework(name, _hook_func)   \
static ev_thd_hook_t ev_thd_prework_hook_ ## name ## _ ##_hook_func = { .hook_func = _hook_func };   \
static void ev_thd_prework_hook_ ## name ## _ ##_hook_func ## _reg() attr_ctor(CTOR_PRIO_LOW);   \
static void ev_thd_prework_hook_ ## name ## _ ##_hook_func ## _reg() \
{   \
    ev_thd_hook_reg(&ev_thd_attr_ ## name, &ev_thd_prework_hook_ ## name ## _ ##_hook_func, EV_THD_HOOK_TYPE_PREWORK); \
}   \
/* ev_thd_hook_prework_reg */

/**
 * 外部使用，注册postwork钩子
 */
#define declare_ev_thd_postwork(name, _hook_func)   \
static ev_thd_hook_t ev_thd_postwork_hook_ ## name ## _ ##_hook_func = { .hook_func = _hook_func };   \
static void ev_thd_postwork_hook_ ## name ## _ ##_hook_func ## _reg() attr_ctor(CTOR_PRIO_LOW);   \
static void ev_thd_postwork_hook_ ## name ## _ ##_hook_func ## _reg() \
{   \
    ev_thd_hook_reg(&ev_thd_attr_ ## name, &ev_thd_postwork_hook_ ## name ## _ ##_hook_func, EV_THD_HOOK_TYPE_POSTWORK); \
}   \
/* ev_thd_hook_postwork_reg */

/**
 * 外部使用，注册dtor钩子
 */
#define declare_ev_thd_dtor(name, _hook_func)   \
static ev_thd_hook_t ev_thd_dtor_hook_ ## name ## _ ##_hook_func = { .hook_func = _hook_func };   \
static void ev_thd_dtor_hook_ ## name ## _ ##_hook_func ## _reg() attr_ctor(CTOR_PRIO_LOW);   \
static void ev_thd_dtor_hook_ ## name ## _ ##_hook_func ## _reg() \
{   \
    ev_thd_hook_reg(&ev_thd_attr_ ## name, &ev_thd_dtor_hook_ ## name ## _ ##_hook_func, EV_THD_HOOK_TYPE_DTOR); \
}   \
/* ev_thd_hook_dtor_reg */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       init ev thd attr, add into global hash
 * 
 * @param[in]   attr    - ev thd attr
 */
extern void ev_thd_attr_init(ev_thd_attr_t *attr);

/**
 * @brief       run an ev thd
 * 
 * @param[in]   attr    - ev thd attr
 */
extern void _ev_thd_run(ev_thd_attr_t *attr);

/**
 * @brief       wake an ev thd
 * 
 * @param[in]   attr    - ev thd attr
 */
extern void _ev_thd_wake(ev_thd_attr_t *attr);

/**
 * @brief       cancel an ev thd
 * 
 * @param[in]   attr    - ev thd attr
 */
extern void _ev_thd_cancel(ev_thd_attr_t *attr);

/**
 * @brief       register hook to attr
 * 
 * @param[in]   attr    - ev thd attr
 * @param[in]   hook    - hook item
 * @param[in]   type    - hook type
 */
extern void ev_thd_hook_reg(ev_thd_attr_t *attr, ev_thd_hook_t *hook, ev_thd_hook_type_e type);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

#endif