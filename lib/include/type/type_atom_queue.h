/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    type_atomic_queue.h
 * @brief   通用原子队列头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-05-08
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-05-08 | cai | Initial creation.
 */

#ifndef __TYPE_ATOM_QUEUE_H__
#define __TYPE_ATOM_QUEUE_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <assert.h>
#include <string.h>
#include "plat/atom.h"
#include "plat/compiler.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 队列节点定义
typedef struct{
    ATOMIC_UINTPTR_T next;
}spsc_atom_queue_item_t;

// 队列管理结构定义
typedef struct{
    spsc_atom_queue_item_t first_item;      // 哨兵节点
    ATOMIC_UINTPTR_T last_next;             // 指向队尾的next
    unsigned int count;                     // 队列长度
}spsc_atom_queue_head_t;

typedef struct mpsc_atom_queue_head_s mpsc_atom_queue_head_t;   // mpsc队列声明

typedef struct mpmc_atom_queue_head_s mpmc_atom_queue_head_t;   // mpmc队列声明，对外隐藏成员信息

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define pre_declare_spsc_atom_queue(sprefix)    \
typedef struct { spsc_atom_queue_item_t spsc_aq_item; } sprefix ## _spsc_atom_queue_item_t; \
typedef struct { spsc_atom_queue_head_t spsc_aq_head; } sprefix ## _spsc_atom_queue_head_t; \
/* pre_declare_spsc_atom_queue end */

#define declare_spsc_atom_queue(sprefix, fprefix, type, field)  \
static attr_force_inline void fprefix ## _spsc_atom_queue_init(sprefix ## _spsc_atom_queue_head_t *head)    \
{ \
    assert(head);   \
    memset(head, 0, sizeof(*head)); \
    type_spsc_atom_queue_init(&head->spsc_aq_head); \
} \
static attr_force_inline void fprefix ## _spsc_atom_queue_fini(sprefix ## _spsc_atom_queue_head_t *head)    \
{ \
    assert(head);   \
    memset(head, 0, sizeof(*head)); \
} \
static attr_force_inline void fprefix ## _spsc_atom_queue_push(sprefix ## _spsc_atom_queue_head_t *head, type *item)    \
{ \
    assert(head && item);   \
    type_spsc_atom_queue_push(&head->spsc_aq_head, &item->field.spsc_aq_item);  \
} \
static attr_force_inline type* fprefix ## _spsc_atom_queue_pop(sprefix ## _spsc_atom_queue_head_t *head)    \
{ \
    assert(head);   \
    spsc_atom_queue_item_t *it = NULL;  \
    it = type_spsc_atom_queue_pop(&head->spsc_aq_head); \
    return it ? container_of(it, type, field.spsc_aq_item) : NULL;  \
} \
static attr_force_inline unsigned int fprefix ## _spsc_atom_queue_count(sprefix ## _spsc_atom_queue_head_t *head)   \
{ \
    return head ? type_spsc_atom_queue_count(&head->spsc_aq_head) : 0;  \
} \
/* declare_spsc_atom_queue end */

/* ========================================================================== */
/*                            Function Prototypes                             */
/* ========================================================================== */

/**
 * @brief       init single producer single consumer atom queue
 *
 * @param[in]   head        spsc atom queue head
 * 
 * @note        
 */
static attr_force_inline void type_spsc_atom_queue_init(spsc_atom_queue_head_t *head);

/**
 * @brief       push item into single producer single consumer atom queue
 *
 * @param[in]   head        spsc atom queue head
 * @param[in]   item        item
 * 
 * @note        
 */
extern void type_spsc_atom_queue_push(spsc_atom_queue_head_t *head, spsc_atom_queue_item_t *item);

/**
 * @brief       pop item from single producer single consumer atom queue
 *
 * @param[in]   head        spsc atom queue head
 * 
 * @retval      ptr to spsc atom queue item
 * 
 * @note        
 */
extern spsc_atom_queue_item_t* type_spsc_atom_queue_pop(spsc_atom_queue_head_t *head);

/**
 * @brief       get count of single producer single consumer atom queue
 *
 * @param[in]   head        spsc atom queue head
 * 
 * @retval      counts
 */
static attr_force_inline unsigned int type_spsc_atom_queue_count(spsc_atom_queue_head_t *head);

/**
 * @brief       init mpsc atom queue
 */
extern void mpsc_atom_queue_init(mpsc_atom_queue_head_t **head);

/**
 * @brief       push into mpsc aq
 * 
 * @param[in]   head    - aq head
 * @param[in]   data    - data
 */
extern void mpsc_atom_queue_push(mpsc_atom_queue_head_t *head, void *data);

/**
 * @brief       pop from mpsc aq
 * 
 * @param[in]   aq_head     - head
 * 
 * @retval      user data
 */
extern void* mpsc_atom_queue_pop(mpsc_atom_queue_head_t *aq_head);

/**
 * @brief       init mpmc aq
 * 
 * @param[in]   head    - aq head
 * 
 * @note        申请哨兵节点，初始head,tail均指向，版本号0
 */
extern void mpmc_atom_queue_init(mpmc_atom_queue_head_t **head);

/**
 * @brief       push data into mpmc aq
 * 
 * @param[in]   head    - aq head
 * @param[in]   data    - ptr to data
 * 
 * @note        基于MS算法push
 */
extern void mpmc_atom_queue_push(mpmc_atom_queue_head_t *head, void *data);

/**
 * @brief       pop data from mpmc aq
 * 
 * @param[in]   head    - aq head
 * 
 * @note        基于MS算法pop
 */
extern void* mpmc_atom_queue_pop(mpmc_atom_queue_head_t *head);

/* ========================================================================== */
/*                         Private Function Implementations                   */
/* ========================================================================== */

static inline void type_spsc_atom_queue_init(spsc_atom_queue_head_t *head)
{
    ATOM_STORE(&head->last_next, ATOM_PTR2UNIT(&head->first_item.next), MORDER_RELAXED);
}

static inline unsigned int type_spsc_atom_queue_count(spsc_atom_queue_head_t *head)
{
    return ATOM_LOAD(&head->count, MORDER_RELAXED);
}

#endif
/* __TYPE_ATOM_QUEUE_H__ end*/