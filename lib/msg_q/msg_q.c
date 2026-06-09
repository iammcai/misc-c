/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    msg_q.c
 * @brief   消息队列实现
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

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "msg_q/msg_q.h"

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

msg_q_ret_type_e _msg_q_push(msg_q_t *msg_q, void *ctx, size_t len)
{
    assert(msg_q && ctx && len <= msg_q->elem_size);
    size_t size = 0;

    ev_with_spinlock(&msg_q->spinlock_in)
    {
        if(ATOM_LOAD(&msg_q->size, MORDER_ACQUIRE) < msg_q->capacity)
        {
            // 入队
            memcpy(msg_q->ring_buffer + msg_q->tail*msg_q->elem_size, ctx, len);
            ++ msg_q->tail;
            if(msg_q->tail >= msg_q->capacity)  // 回绕
                msg_q->tail = 0;
            size = ATOM_FETCH_ADD(&msg_q->size, 1, MORDER_RELEASE);
        }
        else
            return msg_q_ret_full;
    }

    if(!size)   // 入队前为空，通知可以pop
        ev_sem_post(&msg_q->sem_out);

    return msg_q_ret_ok;
}

msg_q_ret_type_e _msg_q_pop(msg_q_t *msg_q, size_t len, msg_q_wait_type_e wait_type, void *ctx)
{
    assert(msg_q && len <= msg_q->elem_size && ctx);

    while(ATOM_LOAD(&msg_q->size, MORDER_ACQUIRE) == 0)
    {
        if(msg_q_no_wait == wait_type)
            return msg_q_ret_non_msg;
        else
            ev_sem_wait(&msg_q->sem_out);       // 等待
    }

    // 获取数据
    memcpy(ctx, msg_q->ring_buffer + msg_q->head*msg_q->elem_size, len);
    ++ msg_q->head;
    if(msg_q->head >= msg_q->capacity)
        msg_q->head = 0;

    ATOM_FETCH_SUB(&msg_q->size, 1, MORDER_RELEASE);    // -1

    return msg_q_ret_ok;
}