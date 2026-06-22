/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    thp.h
 * @brief   线程池头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-21
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-21 | cai | Initial creation.
 */

#ifndef __THP_H__
#define __THP_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <pthread.h>
#include "plat/compiler.h"
#include "plat/debug.h"
#include "type/type_hash.h"
#include "type/type_list.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 工作回调函数签名
typedef void (*thp_work_func)(void *args);

// 预定义哈希表
pre_declare_hash(thp)
// 预定义链表用来存工作任务
pre_declare_list(thp_work)

// 线程池管理结构定义
typedef struct{
    const char *name;           // 线程池名字

    unsigned char thread_count; // 工作线程数量
    pthread_t *thread_array;    // 工作线程

    pthread_mutex_t mtx;        // 互斥锁
    pthread_cond_t cond;        // 条件变量
    thp_work_list_head_t wl;    // 工作队列

    unsigned char shutdown;     // 线程池关闭标志

    thp_hash_item_t item;       // 哈希表Item
}thp_t;

// 工作任务定义
typedef struct{
    thp_work_func wf;           // 回调函数
    void *args;                 // 参数
    thp_work_list_item_t item;  // list item
}thp_work_t;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define THREAD_POOL_TH_COUNT_MAX    (4)

/**
 * 外部使用，声明线程池
 *      _name            名字
 *      _thread_count    线程数量
 */
#define declare_thp(_name, _thread_count)   \
static thp_t _thp_ ## _name = { \
    .name = #_name, \
    .thread_count = _thread_count,  \
    .mtx = PTHREAD_MUTEX_INITIALIZER,   \
    .cond = PTHREAD_COND_INITIALIZER,   \
    .shutdown = 1,  \
};  \
static void _thp_ ## _name ## _init(void) attr_ctor(CTOR_PRIO_LOW); \
static void _thp_ ## _name ## _init(void)   \
{   \
    _thp_init(&_thp_ ## _name); \
}   \
/* declare_thp end */

/**
 * 外部使用，线程池开启工作
 */
#define thp_run(_name)  \
    _thp_run(&_thp_ ## _name);  \
/* thp_run end */

/**
 * 外部使用，向线程池提交任务
 *  _name   thp name
 *  wf      work func
 *  wa      work args
 */
#define thp_submit_task(_name, wf, wa)  \
    _thp_submit_task(&_thp_ ## _name, wf, wa);  \
/* thp_submit_task end */

/**
 * 外部使用，关闭线程池
 *  _name   thp name
 */
#define thp_shutdown(_name) \
    _thp_shutdown(&_thp_ ## _name); \
/* thp_shutdown end */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       init thp var
 * 
 * @param[in]   thp - thread pool
 * 
 * @note        初始化thp，加入哈希表
 */
extern void _thp_init(thp_t *thp);

/**
 * @brief       外部使用，线程池初始化
 * 
 * @param[in]   thp             - ptr to thp
 * @param[in]   name            - 线程池名字
 * @param[in]   thread_count    - 线程数量
 * 
 * @note        效果同declare_thp，区别是在函数中使用
 */
extern void thp_init(thp_t *thp, const char *name, unsigned char thread_count);

/**
 * @brief       run thp
 * 
 * @param[in]   thp - thread pool
 * 
 * @note        启动线程池
 */
extern void _thp_run(thp_t *thp);

/**
 * @brief       submit a task into thread pool
 * 
 * @param[in]   thp     - thread pool
 * @param[in]   wf      - work func
 * @param[in]   args    - args
 */
extern void _thp_submit_task(thp_t *thp, thp_work_func wf, void *args);

/**
 * @brief       shutdown thread pool
 * 
 * @param[in]   thp     - thread pool
 * 
 * @note        关闭线程池
 */
extern void _thp_shutdown(thp_t *thp);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

#endif