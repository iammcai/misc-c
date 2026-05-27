/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    type_hash.h
 * @brief   通用哈希表定义实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-04-30
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-04-30 | cai | Initial creation.
 */

#ifndef __TYPE_HASH_H__
#define __TYPE_HASH_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <assert.h>
#include <stdlib.h>
#include "plat/compiler.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 哈希表节点定义
typedef struct hash_item_s{
    struct hash_item_s *next;
    unsigned int hash_val;
}hash_item_t;

// 哈希表定义
typedef struct{
    hash_item_t **entries;
    unsigned int count;
    unsigned char tab_shift;
    unsigned char min_shift;
    unsigned char max_shift;
}hash_head_t;

// 哈希表大小操作枚举
typedef enum{
    hash_size_opt_grow,     // 扩容
    hash_size_opt_shrink,   // 缩容
}hash_size_opt_e;

typedef int (*type_hash_cmp_f)(const hash_item_t *it1, const hash_item_t *it2);
typedef unsigned int (*type_hash_hash_f)(const hash_item_t *it);

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 根据shift计算哈希桶数量
#define hash_size(shift)        (1U << shift)
// 取hashval的高位来决定放入哪个桶
#define hash_key(shift, val)    (val >> (32 - shift))
// 根据n计算如何设置shift能容纳
#define hash_shift(n)           (32 - __builtin_clz(n))     // __builtin_clz(n)，计算n的二进制表示中前导0的个数，要求n大于0

#define pre_declare_hash(sprefix)   \
typedef struct { hash_item_t hash_item; } sprefix ## _hash_item_t;  \
typedef struct { hash_head_t hash_head; } sprefix ## _hash_head_t;  \
/* pre_declare_hash end */

#define declare_hash(sprefix, fprefix, type, field, min_shift, max_shift, cmp_func, hash_func)  \
static attr_force_inline void fprefix ## _hash_init(sprefix ## _hash_head_t *head)  \
{ \
    assert(head);   \
    type_hash_init(&head->hash_head, min_shift, max_shift); \
} \
static attr_force_inline void fprefix ## _hash_fini(sprefix ## _hash_head_t *head)  \
{ \
    assert(head && !head->hash_head.count); \
    type_hash_fini(&head->hash_head);   \
}\
static attr_force_inline int fprefix ## _hash_cmp(const hash_item_t *it1, const hash_item_t *it2)   \
{ \
    assert(it1 && it2); \
    return cmp_func(container_of(it1, type, field.hash_item), container_of(it2, type, field.hash_item));    \
} \
static attr_pure_inline unsigned int fprefix ## _hash_hash(const hash_item_t *it)   \
{\
    assert(it); \
    return hash_func(container_of(it, type, field.hash_item));  \
}\
static attr_force_inline type* fprefix ## _hash_add(sprefix ## _hash_head_t *head, type *item)  \
{\
    assert(head && item);   \
    hash_item_t *it = NULL; \
    it = type_hash_add(&head->hash_head, &item->field.hash_item, fprefix ## _hash_cmp, fprefix ## _hash_hash);  \
    if(type_hash_resize_tresh_up(&head->hash_head)) \
        type_hash_resize(&head->hash_head, hash_size_opt_grow); \
    return it ? container_of(it, type, field.hash_item) : NULL; \
}\
static attr_force_inline type* fprefix ## _hash_del(sprefix ## _hash_head_t *head, type *item)  \
{ \
    assert(head && item);   \
    hash_item_t *it = NULL; \
    it = type_hash_del(&head->hash_head, &item->field.hash_item);   \
    if(!it) \
        return NULL;    \
    if(type_hash_resize_tresh_down(&head->hash_head))   \
        type_hash_resize(&head->hash_head, hash_size_opt_shrink);   \
    return item;    \
} \
static attr_pure_inline type* fprefix ## _hash_find(sprefix ## _hash_head_t *head, type *item)  \
{ \
    assert(head && item);   \
    hash_item_t *it = NULL; \
    it = type_hash_find(&head->hash_head, &item->field.hash_item, fprefix ## _hash_cmp, fprefix ## _hash_hash); \
    return it ? container_of(it, type, field.hash_item) : NULL; \
} \
static attr_pure_inline type* fprefix ## _hash_first(sprefix ## _hash_head_t *head) \
{ \
    assert(head);   \
    hash_item_t *it = NULL; \
    it = type_hash_first(&head->hash_head); \
    return it ? container_of(it, type, field.hash_item) : NULL; \
} \
static attr_pure_inline type* fprefix ## _hash_next(sprefix ## _hash_head_t *head, type *item)  \
{ \
    assert(head && item);   \
    hash_item_t *it = NULL; \
    it = type_hash_next(&head->hash_head, &item->field.hash_item);  \
    return it ? container_of(it, type, field.hash_item) : NULL; \
} \
static attr_pure_inline unsigned int fprefix ## _hash_count(sprefix ## _hash_head_t *head)  \
{ \
    return type_hash_count(&head->hash_head);   \
} \
/* declare_hash end */

/* ========================================================================== */
/*                            Function Prototypes                             */
/* ========================================================================== */

/**
 * @brief       Init hash
 *
 * @param[in]   head        hash head
 * @param[in]   min_shift   min shift, range [1,31]
 * @param[in]   max_shift   max shift, range [min_shift, 31]
 * 
 * @note        桶数量为 (1 << shift)，初始桶数量为 (1 << min_shift)
 */
extern void type_hash_init(hash_head_t *head, unsigned char min_shift, unsigned char max_shift);

/**
 * @brief       Finish hash
 *
 * @param[in]   head        hash head
 * 
 * @note       
 */
extern void type_hash_fini(hash_head_t *head);

/**
 * @brief       add item into hash
 *
 * @param[in]   head        hash head
 * @param[in]   item        hash item
 * @param[in]   cmp_func    cmp func between hash item
 * @param[in]   hash_func   hash func of hash item
 * 
 * @retval      ptr to hash item
 */
extern hash_item_t* type_hash_add(hash_head_t *head, hash_item_t *item, type_hash_cmp_f cmp_func, type_hash_hash_f hash_func);

/**
 * @brief       del item from hash
 *
 * @param[in]   head        hash head
 * @param[in]   item        hash item
 * 
 * @retval      ptr to hash item, if exist
 */
extern hash_item_t* type_hash_del(hash_head_t *head, hash_item_t *item);

/**
 * @brief       check if need to tresh up hash
 *
 * @param[in]   head        hash head
 * 
 * @retval      0 - no need, else - need
 * 
 * @note       当前策略：负载因子>1.0时，执行扩容
 */
static attr_pure_inline int type_hash_resize_tresh_up(hash_head_t *head);

/**
 * @brief       check if need to tresh down hash
 *
 * @param[in]   head        hash head
 * 
 * @retval      0 - no need, else - need
 * 
 * @note       当前策略：负载因子<0.5时，执行缩容
 */
static attr_pure_inline int type_hash_resize_tresh_down(hash_head_t *head);

/**
 * @brief       check if need to tresh up hash
 *
 * @param[in]   head        hash head
 * @param[in]   opt         grow or shrink?
 * 
 * @note
 */
static attr_force_inline void type_hash_resize(hash_head_t *head, hash_size_opt_e opt);

/**
 * @brief       caculate hash shift
 *
 * @param[in]   head        hash head
 * 
 * @retval      shift
 * 
 * @note        计算当前应该设置的shift
 */
static attr_force_inline unsigned char type_hash_shift_cal(hash_head_t *head);

/**
 * @brief       grow hash bucket
 *
 * @param[in]   head        hash head
 * @param[in]   new_shift   new shift 
 * 
 * @note        哈希扩容
 */
extern void type_hash_grow(hash_head_t *head, unsigned char new_shift);

/**
 * @brief       shrink hash bucket
 *
 * @param[in]   head        hash head
 * @param[in]   new_shift   new shift 
 * 
 * @note        哈希缩容
 */
extern void type_hash_shrink(hash_head_t *head, unsigned char new_shift);

/**
 * @brief       find item in hash
 *
 * @param[in]   head        hash head
 * @param[in]   item        hash item
 * @param[in]   cmp_func    cmp func between hash item
 * @param[in]   hash_func   hash func of hash item
 * 
 * @retval      ptr - ptr to hash item, NULL - not found
 */
extern hash_item_t* type_hash_find(hash_head_t *head, hash_item_t *item, type_hash_cmp_f cmp_func, type_hash_hash_f hash_func);

/**
 * @brief       get first item of hash
 *
 * @param[in]   head        hash head
 * 
 * @retval      ptr to first item
 */
static attr_pure_inline hash_item_t* type_hash_first(hash_head_t *head);

/**
 * @brief       get next of item in hash
 *
 * @param[in]   head        hash head
 * @param[in]   item        item
 * 
 * @retval      ptr to item next
 */
static attr_pure_inline hash_item_t* type_hash_next(hash_head_t *head, hash_item_t *item);

/**
 * @brief       get count of items in hash
 *
 * @param[in]   head        hash head
 * 
 * @retval      count of items in hash
 */
static attr_pure_inline unsigned int type_hash_count(hash_head_t *head);

// JHASH func
extern attr_pure unsigned int jhash(const void *key, unsigned int length, unsigned int initval);
extern attr_pure unsigned int jhash2(const unsigned int *k, unsigned int length, unsigned int initval);
extern attr_const unsigned int jhash_3words(unsigned int a, unsigned int b, unsigned int c, unsigned int initval);
// type_list encapsulation
static attr_const_inline unsigned int jhash_2words(unsigned int a, unsigned int b, unsigned int initval);
static attr_const_inline unsigned int jhash_1word(unsigned int a, unsigned int initval);
// type_list top encapsulation, for user

/**
 * @brief       calculate hash val of any type buffer
 *
 * @param[in]   key     需要计算哈希值的缓冲区指针
 * @param[in]   length  缓冲区长度
 * @param[in]   initval 初始种子值，可以不使用，传0
 * 
 * @retval      hash value
 */
static attr_pure_inline unsigned int type_hash_jhash(const void *key, unsigned int length, unsigned int initval);

/**
 * @brief       calculate hash val of u32 array
 *
 * @param[in]   key     需要计算哈希值的u32数组指针
 * @param[in]   length  数组长度
 * @param[in]   initval 初始种子值，可以不使用，传0
 * 
 * @retval      hash value
 */
static attr_pure_inline unsigned int type_hash_jhash2(const unsigned int *key, unsigned int length, unsigned int initval);

/**
 * @brief       calculate hash val of single u32 variable
 *
 * @param[in]   a       需要计算哈希值的u32变量
 * @param[in]   initval 初始种子值，可以不使用，传0
 * 
 * @retval      hash value
 */
static attr_const_inline unsigned int type_hash_jhash_32bit(unsigned int a, unsigned int initval);

/**
 * @brief       calculate hash val of two u32 variables
 *
 * @param[in]   a           需要计算哈希值的u32变量
 * @param[in]   b           需要计算哈希值的u32变量
 * @param[in]   initval     初始种子值，可以不使用，传0
 * 
 * @retval      hash value
 */
static attr_const_inline unsigned int type_hash_jhash_64bit(unsigned int a, unsigned int b, unsigned int initval);

/**
 * @brief       calculate hash val of three u32 variables
 *
 * @param[in]   a           需要计算哈希值的u32变量
 * @param[in]   b           需要计算哈希值的u32变量
 * @param[in]   c           需要计算哈希值的u32变量
 * @param[in]   initval     初始种子值，可以不使用，传0
 * 
 * @retval      hash value
 */
static attr_const_inline unsigned int type_hash_jhash_96bit(unsigned int a, unsigned int b, unsigned int c, unsigned int initval);

/* ========================================================================== */
/*                         Static Function Implementations                    */
/* ========================================================================== */

static inline int type_hash_resize_tresh_up(hash_head_t *head)
{
    if(head->tab_shift == head->max_shift)
        return 0;

    return head->count >= hash_size(head->tab_shift);
}

static inline int type_hash_resize_tresh_down(hash_head_t *head)
{
    if(head->tab_shift == head->min_shift)
        return 0;

    return head->count <= hash_size(head->tab_shift) / 2 - 1;
}

static inline void type_hash_resize(hash_head_t *head, hash_size_opt_e opt)
{
    unsigned char new_shift = 0;

    new_shift = type_hash_shift_cal(head);
    if(new_shift == head->tab_shift)
        return;

    if(hash_size_opt_grow == opt)
        type_hash_grow(head, new_shift);
    else
        type_hash_shrink(head, new_shift);
}

static inline unsigned char type_hash_shift_cal(hash_head_t *head)
{
    unsigned char new_shift = 0;
    unsigned int count = 0;

    count = head->count ? head->count : 1;

    new_shift = hash_shift(count);
    new_shift = new_shift > head->max_shift ? head->max_shift : new_shift;
    new_shift = new_shift < head->min_shift ? head->min_shift : new_shift;

    return new_shift;
}

static inline hash_item_t* type_hash_first(hash_head_t *head)
{
    unsigned int i = 0;
    unsigned int size = 0;

    assert(head->entries);

    size = hash_size(head->tab_shift);

    for(i = 0; i < size; ++ i)
    {
        if(head->entries[i])
            return head->entries[i];
    }

    return NULL;
}

static inline hash_item_t* type_hash_next(hash_head_t *head, hash_item_t *item)
{
    unsigned int i = 0;
    unsigned int size = 0;

    assert(head->entries);

    if(item->next)
        return item->next;

    // 刚好在桶底，找下一个桶的头
    size = hash_size(head->tab_shift);
    for(i = hash_key(head->tab_shift, item->hash_val)+1; i < size; ++ i)
    {
        if(head->entries[i])
            return head->entries[i];
    }

    return NULL;
}

static inline unsigned int type_hash_count(hash_head_t *head)
{
    return !head ? 0 : head->count;
}

static inline unsigned int jhash_1word(unsigned int a, unsigned int initval)
{
    return jhash_3words(a, 0, 0, initval);
}

static attr_const_inline unsigned int jhash_2words(unsigned int a, unsigned int b, unsigned int initval)
{
    return jhash_3words(a, b, 0, initval);
}

static inline unsigned int type_hash_jhash(const void *key, unsigned int length, unsigned int initval)
{
    return jhash(key, length, initval);
}

static inline unsigned int type_hash_jhash2(const unsigned int *key, unsigned int length, unsigned int initval)
{
    return jhash2(key, length, initval);
}

static inline unsigned int type_hash_jhash_32bit(unsigned int a, unsigned int initval)
{
    return jhash_1word(a, initval);
}

static inline unsigned int type_hash_jhash_64bit(unsigned int a, unsigned int b, unsigned int initval)
{
    return jhash_2words(a, b, initval);
}

static inline unsigned int type_hash_jhash_96bit(unsigned int a, unsigned int b, unsigned int c, unsigned int initval)
{
    return jhash_3words(a, b, c, initval);
}

#endif
/* __TYPE_HASH_H__ end */