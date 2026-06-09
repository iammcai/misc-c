/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    msg_q.h
 * @brief   消息队列头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-05
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-05 | cai | Initial creation.
 */

#ifndef __MSG_Q_H__
#define __MSG_Q_H__

 /* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "plat/compiler.h"
#include "event/ev_lock.h"
#include "event/ev_sem.h"
#include "mp/mp.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 预定义list，用于链接消息
pre_declare_list(msg_q)

// 消息队列结构定义
typedef struct{
    const char *name;           // 消息队列名
    size_t head;                // 头节点Index
    size_t tail;                // 尾节点Index+1
    size_t capacity;            // 最大容量
    size_t elem_size;           // 消息大小
    size_t size;                // 当前长度
    ev_spinlock_t spinlock_in;  // 自旋锁，用于互斥写入消息
    ev_sem_t sem_out;           // 信号量，用于通知读取消息
    char ring_buffer[];         // 柔性数组，环形缓冲区
}msg_q_t;

// 消息定义
typedef struct{
    void *ctx;
    msg_q_list_item_t item;
}msg_t;

// 等待类型枚举
typedef enum{
    msg_q_no_wait,
    msg_q_wait_forever
}msg_q_wait_type_e;

// 消息队列错误码枚举
typedef enum{
    msg_q_ret_ok = 0,
    msg_q_ret_full,
    msg_q_ret_non_msg,
}msg_q_ret_type_e;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/**
 * 外部使用，声明定义一个消息队列
 */
#define declare_msg_q(_name, _capacity, _elem_size) \
static msg_q_t* msg_q_ ## _name = NULL; \
static attr_force_inline void _msg_q_ ## _name ## _init() attr_ctor(CTOR_PRIO_MID); \
static attr_force_inline void _msg_q_ ## _name ## _init()   \
{   \
    msg_q_ ## _name = mp_calloc(1, sizeof(msg_q_t) + _capacity * _elem_size);   \
    assert(msg_q_ ## _name);    \
    msg_q_ ## _name->name = #_name; \
    msg_q_ ## _name->capacity = _capacity;  \
    msg_q_ ## _name->elem_size = _elem_size;    \
    ev_spinlock_init(&msg_q_ ## _name->spinlock_in);    \
    ev_sem_init(&msg_q_ ## _name->sem_out);  \
}   \
/* declare_msg_q end */

/**
 * 外部使用，推送消息入队
 */
#define msg_q_push(_name, ctx, len) \
    _msg_q_push(msg_q_ ## _name, ctx, len);    \
/* msg_q_push end */

/**
 * 外部使用，获取消息
 */
#define msg_q_pop(_name, len, _wait_type, ctx)   \
({  \
    _msg_q_pop(msg_q_ ## _name, len, _wait_type, ctx);  \
})  \

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       push msg into msg q
 * 
 * @param[in]   msg_q   - msg q
 * @param[in]   ctx     - msg context
 * @param[in]   len     - msg len
 * 
 * @retval      err code
 */
extern msg_q_ret_type_e _msg_q_push(msg_q_t *msg_q, void *ctx, size_t len);

/**
 * @brief       pop msg from msg q
 * 
 * @param[in]   msg_q       - msg q
 * @param[in]   len         - msg len
 * @param[in]   wait_type   - wait type
 * @param[out]  ctx         - msg context
 * 
 * @retval      err code
 */
extern msg_q_ret_type_e _msg_q_pop(msg_q_t *msg_q, size_t len, msg_q_wait_type_e wait_type, void *ctx);

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

#endif
/* __MSG_Q_H__ end */