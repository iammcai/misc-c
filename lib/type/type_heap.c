/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    type_head.c
 * @brief   侵入式堆实现
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

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "type/type_heap.h"
#include "mp/mp.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 分级进行扩容
#define HEAP_SIZE_L1 (36)
#define HEAP_SIZE_L2 (262144)
#define HEAP_SIZE_L3 (0xAAAA0000U)
#define HEAP_SIZE_L4 (0xBFFFFFFFU)
#define HEAP_DEF_MAX_SIZE (0xFFFFFFFFU)

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       cmp two items in heap
 * 
 * @param[in]   head    - heap head
 * @param[in]   index1  - pos 1
 * @param[in]   index2  - pos 2
 * @param[in]   cmp     - cmp function
 * 
 * @note        内部使用
 */
static attr_pure_inline int _heap_cmp(heap_head_t *head, unsigned int index1, unsigned int index2, type_heap_cmp_func cmp)
{
    return cmp(head->array[index1], head->array[index2]);
}

/**
 * @brief       heap swap two items
 * 
 * @param[in]   head    - heap head
 * @param[in]   index1  - pos 1
 * @param[in]   index2  - pos 2
 * 
 * @note        内部使用
 */
static attr_force_inline void _heap_swap(heap_head_t *head, unsigned int index1, unsigned int index2)
{
    heap_item_t *item = head->array[index1];
    head->array[index1] = head->array[index2];
    head->array[index1]->index = index1;
    head->array[index2] = item;
    item->index = index2;
}

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void type_heap_create(heap_head_t *head, unsigned int min_size, unsigned int max_size)
{
    if(max_size)
        assert(min_size <= max_size);

    head->count = 0;
    head->min_size = min_size;
    head->max_size = max_size ? max_size : HEAP_DEF_MAX_SIZE;
    head->size = min_size;
    if(min_size)        // 申请array内存，记录指向Item的指针
        head->array = mp_calloc(min_size, sizeof(heap_item_t*));
}

void type_heap_destroy(heap_head_t *head)
{
    mp_free(head->array);
    memset(head, 0, sizeof(heap_head_t));
}

void type_heap_resize(heap_head_t *head, heap_size_op_e op)
{
    unsigned int new_size = 0;

    if(HEAP_SIZE_GROW == op)        // 分级扩容
    {
        new_size = head->size;
        if(new_size < HEAP_SIZE_L1)
            new_size = new_size * 2;
        else if(new_size < HEAP_SIZE_L2)
            new_size += new_size / 2;
        else if(new_size < HEAP_SIZE_L3)
            new_size += new_size / 3;
        else if(new_size < HEAP_SIZE_L4)
            new_size += new_size / 4;
        else
            new_size = head->max_size;
    }
    else
        new_size = head->count;

    new_size = new_size < head->min_size ? head->min_size : new_size;
    new_size = new_size > head->max_size ? head->max_size : new_size;

    if(new_size == head->size)
        return;

    if(!new_size)
    {
        mp_free(head->array);
        head->array = NULL;
        head->size = 0;
        return;
    }

    head->array = mp_realloc(head->array, sizeof(heap_item_t*)*new_size);
    head->size = new_size;
}

void type_heap_heapify_up(heap_head_t *head, unsigned int index, type_heap_cmp_func cmp)
{
    unsigned int move_to = 0;

    if(index >= head->size)
        return;

    while(index)    // 交换最多到index == 0
    {
        move_to = (index - 1) >> 1;      // 父节点位置
        if(_heap_cmp(head, move_to, index, cmp) <= 0)   // 父节点不大于，则完成
            break;
        // 交换父节点和当前节点
        _heap_swap(head, move_to, index);
        index = move_to;
    }

    // 最后更新index
    head->array[index]->index = index;
}

void type_heap_heapify_down(heap_head_t *head, unsigned int index, type_heap_cmp_func cmp)
{
    unsigned int lpos = (index << 1) + 1;
    unsigned int rpos = lpos + 1;
    unsigned int move_to = index;

    if(index >= head->size)
        return;

    if(lpos < head->count && _heap_cmp(head, lpos, move_to, cmp) < 0)   // 左孩子大，需要交换
        move_to = lpos;
    if(rpos < head->count && _heap_cmp(head, rpos, move_to, cmp) < 0)   // 同理
        move_to = rpos;

    if(move_to != index)
    {
        _heap_swap(head, index, move_to);
        type_heap_heapify_down(head, move_to, cmp);     // 递归继续
    }
}