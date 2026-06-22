/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    type_head.h
 * @brief   侵入式堆头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-10
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-10 | cai | Initial creation.
 */

#ifndef __TYPE_HEAP_H__
#define __TYPE_HEAP_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <assert.h>
#include <stddef.h>
#include "plat/compiler.h"
#include "plat/debug.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// heap item定义
typedef struct{
    unsigned int index;     // 在数组中的索引
}heap_item_t;

// heap head定义
typedef struct{
    heap_item_t **array;    // 数组
    unsigned int count;     // 当前堆大小
    unsigned int size;      // 当前容量
    unsigned int min_size;  // 最小容量
    unsigned int max_size;  // 最大容量
}heap_head_t;

// heap容量操作类型枚举
typedef enum{
    HEAP_SIZE_GROW = 0,
    HEAP_SIZE_SHRINK
}heap_size_op_e;

// 比较函数签名
typedef int (*type_heap_cmp_func)(const heap_item_t *it1, const heap_item_t *it2);

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/**
 * 外部使用，预定义堆类型
 */
#define pre_declare_heap(sprefix)   \
typedef struct { heap_item_t heap_item; } sprefix ## _heap_item_t;   \
typedef struct { heap_head_t heap_head; } sprefix ## _heap_head_t;   \
/* pre_declare_heap end */

/**
 * 外部使用，定义堆操作
 */
#define declare_heap(sprefix, fprefix, type, field, _min_size, _max_size, cmp_func) \
static attr_force_inline void fprefix ## _heap_init(sprefix ## _heap_head_t *head)  \
{   \
    assert(head);   \
    type_heap_create(&head->heap_head, _min_size, _max_size);   \
}   \
static attr_force_inline void fprefix ## _heap_fini(sprefix ## _heap_head_t *head)  \
{   \
    assert(head);   \
    type_heap_destroy(&head->heap_head);    \
}   \
static attr_pure_inline int fprefix ## _heap_item_cmp(const heap_item_t *it1, const heap_item_t *it2)   \
{   \
    assert(it1 && it2); \
    return cmp_func(container_of(it1, type, field.heap_item), container_of(it2, type, field.heap_item));  \
}   \
static attr_force_inline int fprefix ## _heap_add(sprefix ## _heap_head_t *head, type *item)   \
{   \
    assert(head && item);   \
    if(type_heap_full(&head->heap_head))    \
        return -1; \
    if(type_heap_resize_tresh_up(&head->heap_head)) \
        type_heap_resize(&head->heap_head, HEAP_SIZE_GROW);  \
    unsigned int count = type_heap_count(&head->heap_head); \
    type_heap_add(&head->heap_head, &item->field.heap_item);    \
    type_heap_heapify_up(&head->heap_head, count, fprefix ## _heap_item_cmp);   \
    return 0;   \
}   \
static attr_force_inline type* fprefix ## _heap_pop(sprefix ## _heap_head_t *head)   \
{   \
    assert(head);   \
    if(0 == type_heap_count(&head->heap_head))  \
        return NULL;    \
    heap_item_t *first_item = type_heap_first(&head->heap_head);    \
    unsigned int index = type_heap_del(&head->heap_head, first_item);   \
    type_heap_heapify_down(&head->heap_head, index, fprefix ## _heap_item_cmp); \
    if(type_heap_resize_tresh_down(&head->heap_head))   \
        type_heap_resize(&head->heap_head, HEAP_SIZE_SHRINK);   \
    return container_of(first_item, type, field.heap_item); \
}   \
static attr_pure_inline type* fprefix ## _heap_top(sprefix ## _heap_head_t *head)   \
{   \
    assert(head);   \
    if(0 == type_heap_count(&head->heap_head))  \
        return NULL;    \
    heap_item_t *first_item = type_heap_first(&head->heap_head);    \
    return container_of(first_item, type, field.heap_item); \
}   \
static attr_force_inline void fprefix ## _heap_del(sprefix ## _heap_head_t *head, type *item)   \
{   \
    assert(head && item);   \
    heap_item_t *last_item = type_heap_last(&head->heap_head);  \
    unsigned int index = type_heap_del(&head->heap_head, &item->field.heap_item);   \
    if(cmp_func(container_of(last_item, type, field.heap_item), item) < 0)  \
        type_heap_heapify_up(&head->heap_head, index, fprefix ## _heap_item_cmp);   \
    else    \
        type_heap_heapify_down(&head->heap_head, index, fprefix ## _heap_item_cmp); \
    if(type_heap_resize_tresh_down(&head->heap_head))   \
        type_heap_resize(&head->heap_head, HEAP_SIZE_SHRINK);   \
}   \
static attr_pure_inline unsigned int fprefix ## _heap_count(sprefix ## _heap_head_t *head)  \
{   \
    assert(head);   \
    return type_heap_count(&head->heap_head);   \
}   \
/* declare_heap end */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       create heap, init
 * 
 * @param[in]   head    - heap head
 */
extern void type_heap_create(heap_head_t *head, unsigned int min_size, unsigned int max_size);

/**
 * @brief       destroy heap
 * 
 * @param[in]   head    - heap head
 */
extern void type_heap_destroy(heap_head_t *head);

/**
 * @brief       check if heap full
 * 
 * @param[in]   head    - heap head
 */
static attr_pure_inline int type_heap_full(heap_head_t *head)
{
    return head->count == head->size;
}

/**
 * @brief       check if heap need and can tresh up size
 * 
 * @param[in]   head    - heap head
 * 
 * @retval      1 - yes, 0 - no
 */
static attr_pure_inline int type_heap_resize_tresh_up(heap_head_t *head)
{
    if(head->size == head->max_size)
        return 0;
    return (head->count+1) >= head->size;
}

/**
 * @brief       check if heap need and can tresh down size
 * 
 * @param[in]   head    - heap head
 * 
 * @retval      1 - yes, 0 - no
 */
static attr_pure_inline int type_heap_resize_tresh_down(heap_head_t *head)
{
    if(head->size == head->min_size)
        return 0;
    return (0 == head->count) || (head->size > (head->count * 3 / 2 + 512));
}

/**
 * @brief       resize heap
 * 
 * @param[in]   head    - heap head
 * @param[in]   op      - operation
 */
extern void type_heap_resize(heap_head_t *head, heap_size_op_e op);

/**
 * @brief       get heap count
 * 
 * @param[in]   head    - heap head
 * 
 * @retval      count
 */
static attr_pure_inline unsigned int type_heap_count(heap_head_t *head)
{
    return head ? head->count : 0;
}

/**
 * @brief       add item into heap, not include heapify
 * 
 * @param[in]   head    - heap head
 * @param[in]   item    - item
 * 
 * @note        加到堆尾
 */
static attr_force_inline void type_heap_add(heap_head_t *head, heap_item_t *item)
{
    head->array[head->count] = item;
    head->count ++;
}

/**
 * @brief       rmv item from heap, not include heapify
 * 
 * @param[in]   head    - heap head
 * @param[in]   item    - item
 * 
 * @retval      删除的index
 * 
 * @note        将堆为交换到需要删除的位置
 */
static attr_force_inline unsigned int type_heap_del(heap_head_t *head, heap_item_t *item)
{
    assert(head->array[item->index] == item);

    unsigned int index = item->index;

    head->count --;
    head->array[index] = head->array[head->count];
    head->array[index]->index = index;
    head->array[head->count] = NULL;

    return index;
}

/**
 * @brief       heapify, up
 * 
 * @param[in]   head    - heap head
 * @param[in]   index   - index of item
 * @param[in]   cmp     - cmp func between items
 * 
 * @note        向上调整，插入新元素使用
 */
extern void type_heap_heapify_up(heap_head_t *head, unsigned int index, type_heap_cmp_func cmp);

/**
 * @brief       heapify, down
 * 
 * @param[in]   head    - heap head
 * @param[in]   index   - index of item
 * @param[in]   cmp     - cmp func between items
 * 
 * @note        向下调整，删除元素使用
 */
extern void type_heap_heapify_down(heap_head_t *head, unsigned int index, type_heap_cmp_func cmp);

/**
 * @brief       get heap first item
 * 
 * @param[in]   head    - heap head
 * 
 * @retval      ptr to first item
 */
static attr_pure_inline heap_item_t* type_heap_first(heap_head_t *head)
{
    return head->array[0];
}

/**
 * @brief       get heap last item
 * 
 * @param[in]   head    - heap head
 * 
 * @retval      ptr to last item
 */
static attr_pure_inline heap_item_t* type_heap_last(heap_head_t *head)
{
    return head->array[head->count-1];
}

#endif