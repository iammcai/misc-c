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

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdlib.h>
#include <string.h>
#include "type/type_skiplist.h"
#include "plat/debug.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 随机数转换为level，_builtin_ctz用于统计最低位开始连续0的个数，天然是2次幂的概率（不可输入0）
#define rand_to_level(r)    \
    r == 0 ? 0 : (__builtin_ctz(r))
/* rand_to_level end */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       weak random
 * 
 * @retval      rand number
 */
static attr_force_inline int _weak_random()
{
    return random();    // posix扩展
}

/**
 * @brief       generate level for skiplist
 * 
 * @retval      rand level
 * 
 * @note        level start from 0
 */
static attr_force_inline int _type_skiplist_cal_rand_level()
{
    short rand = 0;
    int level = 0;

    rand = _weak_random();
    level = rand_to_level(rand);

    if(level >= SKIPLIST_DEPTH_MAX)
        level = SKIPLIST_DEPTH_MAX - 1;
    return level;
}

/**
 * @brief       get level next item
 * 
 * @param[in]   item    - sl item
 * @param[in]   level   - sl level
 * 
 * @retval      ptr to item
 */
static attr_force_inline skiplist_item_t* _type_skiplist_get_level_item(skiplist_item_t *item, int level)
{
    return item->next[level];
}

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void type_skiplist_add(skiplist_head_t *head, skiplist_item_t *item, type_skiplist_cmp cmp_func)
{
    int new_level = 0;
    int level = SKIPLIST_DEPTH_MAX - 1;

    skiplist_item_t *prev = NULL;
    skiplist_item_t *next = NULL;

    new_level = _type_skiplist_cal_rand_level();        // 随机生成一个level

    prev = &head->head_item;
    while(level >= new_level)
    {
        next = prev->next[level];
        if(!next)       // 继续往下找
        {
            level --;
            continue;
        }
        // 比较该层节点，找到第一个大于等于item的节点
        if(cmp_func(next, item) < 0)
        {
            prev = next;
            continue;       // 继续横向找
        }
        else        // 本层的next更大，那么该层结束查找，继续往下
            level --;
    }

    // 此时level为下一层Index，item添加在prev和next之间
    memset(item, 0, sizeof(skiplist_item_t));
    prev->next[level+1] = item;
    item->next[level+1] = next;

    // 继续下层添加，低层的prev和next之间有序递增，需要找到第一个大于等于item的
    while(level >= 0)
    {
        next = prev->next[level];
        while(next && cmp_func(next, item) < 0)
        {
            prev = next;
            next = prev->next[level];
        }
        // 此时该level中，item添加到prev和next之间
        prev->next[level] = item;
        item->next[level] = next;
        // 继续往下
        -- level;
    }

    ++ head->count;
}

skiplist_item_t* type_skiplist_del(skiplist_head_t *head, skiplist_item_t *item, type_skiplist_cmp cmp_func)
{
    int found = 0;      // 查找标志
    skiplist_item_t *prev = NULL;
    skiplist_item_t *next = NULL;
    int level = SKIPLIST_DEPTH_MAX - 1;

    prev = &head->head_item;
    while(level >= 0)   // 自上而下查找，逐层删除
    {
        next = prev->next[level];
        while(next && cmp_func(next, item) < 0)
        {
            prev = next;
            next = prev->next[level];
        }
        if(next && 0 == cmp_func(next, item))   // 找到
        {
            found = 1;
            prev->next[level] = next->next[level];
        }
        -- level;   // 往下查找
    }

    if(!found)
        return NULL;

    memset(item, 0, sizeof(*item));
    head->count --;

    return item;
}

skiplist_item_t* type_skiplist_ceil(skiplist_head_t *head, skiplist_item_t *item, type_skiplist_cmp cmp_func)
{
    skiplist_item_t *prev = NULL;
    skiplist_item_t *next = NULL;
    int level = SKIPLIST_DEPTH_MAX-1;

    prev = &head->head_item;
    while(level >= 0)
    {
        next = prev->next[level];
        // 找到第一个大于等于的
        while(next && cmp_func(next, item) < 0)
        {
            prev = next;
            next = prev->next[level];
        }
        -- level;
    }
    // 此时next即为所求
    return next;
}

skiplist_item_t* type_skiplist_floor(skiplist_head_t *head, skiplist_item_t *item, type_skiplist_cmp cmp_func)
{
    skiplist_item_t *prev = NULL;
    skiplist_item_t *next = NULL;
    int level = SKIPLIST_DEPTH_MAX-1;

    prev = &head->head_item;
    while(level >= 0)
    {
        next = prev->next[level];
        // 找到第一个大于等于的
        while(next && cmp_func(next, item) < 0)
        {
            prev = next;
            next = prev->next[level];
        }
        -- level;
    }

    // 此时prev即为所求，如果prev还是哨兵，说明没有
    return prev == &head->head_item ? NULL : prev;
}