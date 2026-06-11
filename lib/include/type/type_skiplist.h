/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    type_skiplist.h
 * @brief   跳表头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-11
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-11 | cai | Initial creation.
 */

#ifndef __TYPE_SKIPLIST_H__
#define __TYPE_SKIPLIST_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <assert.h>
#include <string.h>
#include "plat/compiler.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 跳表最大高度
#define SKIPLIST_DEPTH_MAX  (8)

// 跳表item定义
typedef struct skiplist_item_s{
    struct skiplist_item_s *next[SKIPLIST_DEPTH_MAX];
}skiplist_item_t;

// 跳表head定义
typedef struct{
    skiplist_item_t head_item;      // 头节点
    unsigned int count;             // 跳表长度
}skiplist_head_t;

// 比较item签名
typedef int (*type_skiplist_cmp)(const skiplist_item_t *it1, const skiplist_item_t *it2);

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/**
 * 外部使用，预定义skiplist类型
 */
#define pre_declare_skiplist(sprefix)   \
typedef struct {skiplist_item_t skiplist_item; } sprefix ## _skiplist_item_t;   \
typedef struct {skiplist_head_t skiplist_head; } sprefix ## _skiplist_head_t;   \
/* pre_declare_skiplist end */

/**
 * 外部使用，定义skiplist操作
 */
#define declare_skiplist(sprefix, fprefix, type, field, cmp_func)   \
static attr_force_inline void fprefix ## _skiplist_init(sprefix ## _skiplist_head_t *head)  \
{   \
    assert(head);   \
    memset(head, 0, sizeof(*head)); \
}   \
static attr_force_inline void fprefix ## _skiplist_fini(sprefix ## _skiplist_head_t *head)  \
{   \
    assert(head);   \
    memset(head, 0, sizeof(*head)); \
}   \
static attr_pure_inline int fprefix ## _skiplist_cmp(const skiplist_item_t *it1, const skiplist_item_t *it2)    \
{   \
    return cmp_func(container_of(it1, type, field.skiplist_item), container_of(it2, type, field.skiplist_item));    \
}   \
static attr_pure_inline int fprefix ## _skiplist_unique_cmp(const skiplist_item_t *it1, const skiplist_item_t *it2) \
{   \
    int val = cmp_func(container_of(it1, type, field.skiplist_item), container_of(it2, type, field.skiplist_item)); \
    if(val) \
        return val; \
    return (unsigned long)it1 - (unsigned long)it2; \
}   \
static attr_force_inline void fprefix ## _skiplist_add(sprefix ## _skiplist_head_t *head, type *item)   \
{   \
    assert(head && item);   \
    type_skiplist_add(&head->skiplist_head, &item->field.skiplist_item, fprefix ## _skiplist_unique_cmp);   \
}   \
static attr_force_inline type* fprefix ## _skiplist_del(sprefix ## _skiplist_head_t *head, type *item)  \
{   \
    assert(head && item);   \
    skiplist_item_t *it = type_skiplist_del(&head->skiplist_head, &item->field.skiplist_item, fprefix ## _skiplist_unique_cmp);  \
    return it ? container_of(it, type, field.skiplist_item) : NULL; \
}   \
static attr_pure_inline type* fprefix ## _skiplist_first(sprefix ## _skiplist_head_t *head) \
{   \
    assert(head);   \
    skiplist_item_t *it = type_skiplist_first(&head->skiplist_head);    \
    return it ? container_of(it, type, field.skiplist_item) : NULL; \
}   \
static attr_pure_inline type* fprefix ## _skiplist_last(sprefix ## _skiplist_head_t *head)  \
{   \
    assert(head);   \
    skiplist_item_t *it = type_skiplist_last(&head->skiplist_head); \
    return it ? container_of(it, type, field.skiplist_item) : NULL; \
}   \
static attr_pure_inline type* fprefix ## _skiplist_next(type *item)  \
{   \
    assert(item);   \
    skiplist_item_t *it = type_skiplist_next(&item->field.skiplist_item);   \
    return it ? container_of(it, type, field.skiplist_item) : NULL; \
}   \
static attr_pure_inline unsigned int fprefix ## _skiplist_count(sprefix ## _skiplist_head_t *head)  \
{   \
    return head ? type_skiplist_count(&head->skiplist_head) : 0;    \
}   \
static attr_pure_inline type* fprefix ## _skiplist_ceil(sprefix ## _skiplist_head_t *head, type *item)  \
{   \
    assert(head && item);   \
    skiplist_item_t *it = type_skiplist_ceil(&head->skiplist_head, &item->field.skiplist_item, fprefix ## _skiplist_cmp); \
    return it ? container_of(it, type, field.skiplist_item) : NULL; \
}   \
static attr_pure_inline type* fprefix ## _skiplist_floor(sprefix ## _skiplist_head_t *head, type *item) \
{   \
    assert(head && item);   \
    skiplist_item_t *it = type_skiplist_floor(&head->skiplist_head, &item->field.skiplist_item, fprefix ## _skiplist_cmp);  \
    return it ? container_of(it, type, field.skiplist_item) : NULL; \
}   \
/* declare_skiplist end */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       add item into skiplist
 * 
 * @param[in]   head    - skiplist head
 * @param[in]   item    - skiplist item
 * @param[in]   cmp_func    - compare func
 */
extern void type_skiplist_add(skiplist_head_t *head, skiplist_item_t *item, type_skiplist_cmp cmp_func);

/**
 * @brief       del item form sl
 * 
 * @param[in]   head    - skiplist head
 * @param[in]   item    - skiplist item
 * @param[in]   cmp_func    - compare func
 * 
 * @retval      ptr to item
 */
extern skiplist_item_t* type_skiplist_del(skiplist_head_t *head, skiplist_item_t *item, type_skiplist_cmp cmp_func);

/**
 * @brief       get first item in skiplist
 * 
 * @param[in]   head    - skiplist head
 * 
 * @retval      first (min) item in skiplist
 */
static attr_pure_inline skiplist_item_t* type_skiplist_first(skiplist_head_t *head)
{
    return head->head_item.next[0];
}

/**
 * @brief       get last item in skiplist
 * 
 * @param[in]   head    - skiplist head
 * 
 * @retval      last (max) item in skiplist
 */
static attr_pure_inline skiplist_item_t* type_skiplist_last(skiplist_head_t *head)
{
    skiplist_item_t *it = head->head_item.next[0];
    while(it && it->next[0])
        it = it->next[0];
    return it;
}

static attr_pure_inline skiplist_item_t* type_skiplist_next(skiplist_item_t *item)
{
    return item->next[0];
}

/**
 * @brief       get skiplist count
 * 
 * @param[in]   head    - skiplist head
 * 
 * @retval      count
 */
static attr_pure_inline unsigned int type_skiplist_count(skiplist_head_t *head)
{
    return head->count;
}

/**
 * @brief       找到大于等于Item的最小元素
 * 
 * @param[in]   head    - sl head
 * @param[in]   item    - item
 * @param[in]   cmp_func    - compare func
 * 
 * @retval      target item
 */
extern skiplist_item_t* type_skiplist_ceil(skiplist_head_t *head, skiplist_item_t *item, type_skiplist_cmp cmp_func);

/**
 * @brief       找到小于等于Item的最大元素
 * 
 * @param[in]   head    - sl head
 * @param[in]   item    - item
 * @param[in]   cmp_func    - compare func
 * 
 * @retval      target item
 */
extern skiplist_item_t* type_skiplist_floor(skiplist_head_t *head, skiplist_item_t *item, type_skiplist_cmp cmp_func);

#endif