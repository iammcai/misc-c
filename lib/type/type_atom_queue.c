/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    type_atomic_queue.c
 * @brief   通用原子队列实现
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

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "type/type_atom_queue.h"
#include "mp/mp.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// MPMC队列节点定义
typedef struct{
    void *data;             // 指向用户数据
    ATOMIC_UINTPTR_T next;  // next
}mpmc_atom_queue_item_t;

// 含version的指针定义
typedef struct{
    mpmc_atom_queue_item_t *ptr;
    uint64_t version;
} attr_aligned(16) mpmc_tagged_ptr_t;

// MPMC队列定义
struct mpmc_atom_queue_head_s{
    ATOMIC_UINT128_T head;  // 指向哨兵
    ATOMIC_UINT128_T tail;  // 指向队尾
};

/* ========================================================================== */
/*                         Private Function Implementations                   */
/* ========================================================================== */

/**
 * @brief       trans u128 to tagged ptr
 */
static attr_pure_inline mpmc_tagged_ptr_t u128_to_tagged_ptr(ATOMIC_UINT128_T u)
{
    return *(mpmc_tagged_ptr_t*)&u;
}

/**
 * @brief       trans tagged ptr to u128
 */
static attr_pure_inline ATOMIC_UINT128_T tagged_ptr_to_u128(mpmc_tagged_ptr_t t)
{
    return *(ATOMIC_UINT128_T*)&t;
}

/* ========================================================================== */
/*                         Public Function Implementations                    */
/* ========================================================================== */

void type_spsc_atom_queue_push(spsc_atom_queue_head_t *head, spsc_atom_queue_item_t *item)
{
    uintptr_t prev_next = 0;

    if(0 == ATOM_FETCH_ADD(&head->count, 1, MORDER_SEQ_SCT))        // count ++
        type_spsc_atom_queue_init(head);        // 这里是因为pop最后一个节点后，没有更新lastNext，这里手动更新一下

    ATOM_STORE(&item->next, ATOM_PTR_NULL, MORDER_SEQ_SCT);
    prev_next = ATOM_XCHG(&head->last_next, ATOM_PTR2UNIT(&item->next), MORDER_ACQ_REL);
    ATOM_STORE((ATOMIC_UINTPTR_T*)ATOM_UINT2PTR(prev_next), ATOM_PTR2UNIT(item), MORDER_RELEASE);
}

spsc_atom_queue_item_t* type_spsc_atom_queue_pop(spsc_atom_queue_head_t *head)
{
    uintptr_t first = 0;
    uintptr_t expect = 0;
    uintptr_t next = 0;
    spsc_atom_queue_item_t *it = NULL;
    unsigned int n = 0;

    first = ATOM_LOAD(&head->first_item.next, MORDER_SEQ_SCT);  // 检查是否有节点
    if(!first)
        return NULL;

    expect = first;
    it = ATOM_UINT2PTR(first);
    n = ATOM_SUB_FETCH(&head->count, 1, MORDER_SEQ_SCT);        // count --
    if(n)   // 如果还有，那么更新first->next
    {
        do
        {
            next = ATOM_LOAD(&it->next, MORDER_SEQ_SCT);    // 获取从first->next
        }while(!next);
    }
    ATOM_STORE(&head->first_item.next, next, MORDER_RELEASE);   // 将哨兵Next更新为next

    return it;
}

void mpmc_atom_queue_init(mpmc_atom_queue_head_t **head)
{
    *head = mp_calloc(1, sizeof(mpmc_atom_queue_head_t));

    mpmc_atom_queue_item_t *dummy_item = mp_calloc(1, sizeof(mpmc_atom_queue_item_t));
    dummy_item->next = 0;

    mpmc_tagged_ptr_t dummy_tag = {
        .ptr = dummy_item,
        .version = 0,
    };
    ATOMIC_UINT128_T dummy = tagged_ptr_to_u128(dummy_tag);

    ATOM_STORE(&(*head)->head, dummy, MORDER_SEQ_SCT);
    ATOM_STORE(&(*head)->tail, dummy, MORDER_SEQ_SCT);
}

void mpmc_atom_queue_push(mpmc_atom_queue_head_t *head, void *data)
{
    assert(head && data);

    // 申请新节点
    mpmc_atom_queue_item_t *item = mp_calloc(1, sizeof(mpmc_atom_queue_item_t));
    item->data = data;
    item->next = 0;

    while(1)
    {
        // 获取tail以及tail->next
        ATOMIC_UINT128_T tail = ATOM_LOAD(&head->tail, MORDER_SEQ_SCT);
        mpmc_tagged_ptr_t tail_tag = u128_to_tagged_ptr(tail);
        mpmc_atom_queue_item_t *tail_next = ATOM_UINT2PTR(ATOM_LOAD(&tail_tag.ptr->next, MORDER_SEQ_SCT));

        // 检查tail是否已经被修改了，如果是，重来
        if(tail != ATOM_LOAD(&head->tail, MORDER_SEQ_SCT))
            continue;

        // 检查tail->next是否空，可能别的线程已经修改，但没来得及修改tail
        if(tail_next)
        {
            // 帮助推进tail修改
            mpmc_tagged_ptr_t new_tail_tag = {
                .ptr = tail_next,
                .version = tail_tag.version + 1,
            };
            ATOMIC_UINT128_T new_tail = tagged_ptr_to_u128(new_tail_tag);
            ATOM_CMP_XCHG_WEAK(&head->tail, &tail, new_tail, MORDER_SEQ_SCT, MORDER_SEQ_SCT);
        }
        else
        {
            // 更新tail->next
            mpmc_atom_queue_item_t *expected = NULL;
            if(ATOM_CMP_XCHG_WEAK(&tail_tag.ptr->next, &expected, item, MORDER_SEQ_SCT, MORDER_SEQ_SCT))
            {
                // 更新tail
                mpmc_tagged_ptr_t new_tail_tag = {
                    .ptr = item,
                    .version = tail_tag.version + 1,
                };
                ATOMIC_UINT128_T new_tail = tagged_ptr_to_u128(new_tail_tag);
                ATOM_CMP_XCHG_WEAK(&head->tail, &tail, new_tail, MORDER_SEQ_SCT, MORDER_SEQ_SCT);
                // CAS失败无需重试，其它线程帮忙推进
                return;
            }
        }
    }
}

void* mpmc_atom_queue_pop(mpmc_atom_queue_head_t *aq_head)
{
    assert(aq_head);

    while(1)
    {
        //获取head和tail
        ATOMIC_UINT128_T head = ATOM_LOAD(&aq_head->head, MORDER_SEQ_SCT);
        mpmc_tagged_ptr_t head_tag = u128_to_tagged_ptr(head);
        ATOMIC_UINT128_T tail = ATOM_LOAD(&aq_head->tail, MORDER_SEQ_SCT);
        mpmc_tagged_ptr_t tail_tag = u128_to_tagged_ptr(tail);

        // 检查head是否已经被修改了
        if(head != ATOM_LOAD(&aq_head->head, MORDER_SEQ_SCT))
            continue;

        // 检查head是否等于tail，可能空队列
        // 获取head->next
        mpmc_atom_queue_item_t *head_next = ATOM_UINT2PTR(ATOM_LOAD(&head_tag.ptr->next, MORDER_SEQ_SCT));
        if(head_tag.ptr == tail_tag.ptr)
        {
            // 检查next是否空，可能有线程正在入队，还没来得及修改tail
            if(!head_next)
                return NULL;
            else
            {
                // 帮忙推动tail修改
                mpmc_tagged_ptr_t new_tail_tag = {
                    .ptr = head_next,
                    .version = tail_tag.version + 1,
                };
                ATOMIC_UINT128_T new_tail = tagged_ptr_to_u128(new_tail_tag);
                ATOM_CMP_XCHG_WEAK(&aq_head->tail, &tail, new_tail, MORDER_SEQ_SCT, MORDER_SEQ_SCT);
            }
        }
        else
        {
            // 将head推进到next位置，next作为新的哑节点
            void *data = head_next->data;

            mpmc_tagged_ptr_t new_head_tag = {
                .ptr = head_next,
                .version = head_tag.version + 1,
            };
            ATOMIC_UINT128_T new_head = tagged_ptr_to_u128(new_head_tag);
            if(ATOM_CMP_XCHG_WEAK(&aq_head->head, &head, new_head, MORDER_SEQ_SCT, MORDER_SEQ_SCT))
            {
                // 释放原来的head内存，返回
                mp_free(head_tag.ptr);
                return data;
            }
        }
    }
}