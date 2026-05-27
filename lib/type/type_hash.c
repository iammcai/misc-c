/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    type_hash.c
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

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <string.h>
#include <stdlib.h>
#include "type/type_hash.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 哈希桶数量：1 << shift
#define HASH_MIN_SHIFT      (1)
#define HASH_MAX_SHIFT      (31)

// 比较哈希值
#define hash_val_cmp(a, b, cmp)     (a && b && ((a)->hash_val cmp (b)->hash_val))

/* ========================================================================== */
/*                         Public Function Implementations                    */
/* ========================================================================== */

void type_hash_init(hash_head_t *head, unsigned char min_shift, unsigned char max_shift)
{
    if(max_shift)
        assert(min_shift <= max_shift);

    assert(min_shift >= HASH_MIN_SHIFT && max_shift <= HASH_MAX_SHIFT);

    head->count = 0;
    head->min_shift = head->tab_shift = min_shift;
    head->max_shift = max_shift;

    head->entries = calloc(hash_size(head->tab_shift), sizeof(hash_item_t*));   // TODO: mp
}

void type_hash_fini(hash_head_t *head)
{
    free(head->entries);    // TODO: mp
    memset(head, 0, sizeof(hash_head_t));
}

hash_item_t* type_hash_add(hash_head_t *head, hash_item_t *item, type_hash_cmp_f cmp_func, type_hash_hash_f hash_func)
{
    unsigned int high_bits = 0;
    hash_item_t **it = NULL;

    assert(head->entries);
    assert(cmp_func && hash_func);

    item->hash_val = hash_func(item);
    high_bits = hash_key(head->tab_shift, item->hash_val);  // 计算放入的桶的索引
    it = &head->entries[high_bits];

    // 找到首个hash_val不小于item的hash_val的节点
    while(hash_val_cmp(*it, item, <))
        it = &(*it)->next;

    // 处理哈希碰撞，跳过所有hash_val一致的item，如果业务数据也一致，那么说明已存在，直接返回
    while(hash_val_cmp(*it, item, ==))
    {
        if(!cmp_func(*it, item))
            return *it;
        it = &(*it)->next;
    }

    // 修改link，item插入到it前边
    item->next = *it;
    *it = item;

    head->count ++;

    return NULL;
}

hash_item_t* type_hash_del(hash_head_t *head, hash_item_t *item)
{
    unsigned int high_bits = 0;
    hash_item_t **it = NULL;

    assert(head->entries);

    high_bits = hash_key(head->tab_shift, item->hash_val);  // 获取item所在桶索引
    it = &head->entries[high_bits];

    while(hash_val_cmp(*it, item, <))   // 跳过所有hash_val小于item的
        it = &(*it)->next;

    while(hash_val_cmp(*it, item, ==) && *it != item)   // 处理哈希碰撞
        it = &(*it)->next;

    if(*it != item)     // 查找失败
        return NULL;

    *it = item->next;
    item->next = NULL;
    head->count --;

    return item;
}

void type_hash_grow(hash_head_t *head, unsigned char new_shift)
{
    unsigned char delta = 0;
    unsigned int range = 0;
    unsigned int old_size = 0;
    unsigned int new_size = 0;
    unsigned int i = 0;

    assert(head->entries);

    delta = new_shift - head->tab_shift;        // delta表示2的幂次的差值
    range = 1U << delta;                        // range表示每个旧桶分裂后的数量，比如2^1->2^3，2->8，每个桶分裂成四个
    old_size = hash_size(head->tab_shift);      // 原来的桶数量
    new_size = hash_size(new_shift);            // 新的桶数量
    head->tab_shift = new_shift;                // 修改新的shift

    head->entries = realloc(head->entries, new_size * sizeof(hash_item_t*));    // TODO: mp
    memset(head->entries + old_size, 0, (new_size - old_size) * sizeof(hash_item_t*));  // 将新桶的头指针置空

    // 核心切分移动逻辑
    // 原来是高shift位决定索引，现在变成高（shift+delta）位
    // 那么新的索引就是(old_size << delta | i)，range表示的是(0, (1 << delta) - 1]，i属于range
    // 由于桶内hash_val递增，所以可以切分，整体挪动
    // 比如原本有桶：11|001 -> 11|010 -> 11|101 -> 11|110
    // 现在扩容delta = 1
    // 那么新的划分应该是：110|01 110|10 111|01 111|10
    // 可见还是连续且递增的，那么可以直接整体移动成为：110|01 -> 110|10 和 111|01 -> 111|10
    // 因为需要从前往后移动，所以循环从大到小来
    do
    {
        hash_item_t *it = NULL;
        hash_item_t **pos = NULL;

        old_size --;

        pos = &head->entries[old_size];
        for(i = 0; i < range; ++ i)             // 遍历分裂的新桶
        {
            unsigned int index = 0;

            it = *pos;                          // 取出当前剩余链表头
            *pos = NULL;                        // 重要：断开前面的next
            index = (old_size << delta) | i;    // 计算得到新桶的索引
            head->entries[index] = it;          // 当前剩余链表的头，暂存到新桶
            pos = &head->entries[index];        // pos指向新桶的头指针的地址

            while((it = *pos))  // 注意这里不是==，用于赋值以及判断NULL
            {
                unsigned int mid_bits = 0;

                mid_bits = hash_key(head->tab_shift, it->hash_val);     // 获取当前的(N+delta)位
                mid_bits &= (range - 1);                                // 提取低delta位

                if(mid_bits > i)        // 如果delta位超过i，说明剩余的不在这个桶里，需要断开
                    break;

                pos = &it->next;        // 继续往下找。这依赖了hash_val有序递增，以及使用高shift位作为桶索引的特性
            }
        }
    }while(old_size > 0);   // 迁移到新桶
}

void type_hash_shrink(hash_head_t *head, unsigned char new_shift)
{
    unsigned char delta = 0;
    unsigned int range = 0;
    unsigned int old_size = 0;
    unsigned int new_size = 0;
    unsigned int i = 0;
    unsigned int j = 0;

    assert(head->entries);

    delta = head->tab_shift - new_shift;
    range = 1U << delta;
    old_size = hash_size(head->tab_shift);
    new_size = hash_size(new_shift);
    head->tab_shift = new_shift;

    for(i = 0; i < new_size; i ++)
    {
        hash_item_t **pos = NULL;

        pos = &head->entries[i];
        for(j = 0; j < range; j ++)
        {
            unsigned index = 0;

            index = (i << delta) | j;       // 计算需要撤除的桶的索引
            *pos = head->entries[index];
            while(*pos)                     // pos维护链表尾部，依次将旧桶连接到新桶，利用的也是hash_val递增，高位作桶索引的特性
                pos = &(*pos)->next;
        }
    }

    head->entries = realloc(head->entries, new_size * sizeof(hash_item_t*));    // TODO: mp
}

hash_item_t* type_hash_find(hash_head_t *head, hash_item_t *item, type_hash_cmp_f cmp_func, type_hash_hash_f hash_func)
{
    unsigned int high_bits = 0;
    hash_item_t *it = NULL;

    assert(head->entries);
    assert(cmp_func && hash_func);

    item->hash_val = hash_func(item);
    high_bits = hash_key(head->tab_shift, item->hash_val);
    it = head->entries[high_bits];

    while(hash_val_cmp(it, item, <))
        it = it->next;

    while(hash_val_cmp(it, item, ==))   // 处理哈希碰撞
    {
        if(!cmp_func(it, item))
            return it;
        it = it->next;
    }

    return NULL;
}

/* ========================================================================== */
/*                                   JHASH                                    */
/* ========================================================================== */

// 以下内容来自JHash，以此为基础封装别名
/* The golden ration: an arbitrary value */
#define JHASH_GOLDEN_RATIO  0x9e3779b9

/* NOTE: Arguments are modified. */
#define __jhash_mix(a, b, c) \
{ \
  a -= b; a -= c; a ^= (c>>13); \
  b -= c; b -= a; b ^= (a<<8); \
  c -= a; c -= b; c ^= (b>>13); \
  a -= b; a -= c; a ^= (c>>12);  \
  b -= c; b -= a; b ^= (a<<16); \
  c -= a; c -= b; c ^= (b>>5); \
  a -= b; a -= c; a ^= (c>>3);  \
  b -= c; b -= a; b ^= (a<<10); \
  c -= a; c -= b; c ^= (b>>15); \
}

/* The most generic version, hashes an arbitrary sequence
 * of bytes.  No alignment or length assumptions are made about
 * the input key.
 */
unsigned int jhash(const void *key, unsigned int length, unsigned int initval)
{
    unsigned int a, b, c, len;
    const unsigned char *k = key;

    len = length;
    a = b = JHASH_GOLDEN_RATIO;
    c = initval;

    while (len >= 12) {
        a += (k[0] + ((unsigned int)k[1] << 8) + ((unsigned int)k[2] << 16)
                + ((unsigned int)k[3] << 24));
        b += (k[4] + ((unsigned int)k[5] << 8) + ((unsigned int)k[6] << 16)
                + ((unsigned int)k[7] << 24));
        c += (k[8] + ((unsigned int)k[9] << 8) + ((unsigned int)k[10] << 16)
                + ((unsigned int)k[11] << 24));

        __jhash_mix(a, b, c);

        k += 12;
        len -= 12;
    }

    c += length;
    switch (len) {
    case 11:
        c += ((unsigned int)k[10] << 24);
    /* fallthru */
    case 10:
        c += ((unsigned int)k[9] << 16);
    /* fallthru */
    case 9:
        c += ((unsigned int)k[8] << 8);
    /* fallthru */
    case 8:
        b += ((unsigned int)k[7] << 24);
    /* fallthru */
    case 7:
        b += ((unsigned int)k[6] << 16);
    /* fallthru */
    case 6:
        b += ((unsigned int)k[5] << 8);
    /* fallthru */
    case 5:
        b += k[4];
    /* fallthru */
    case 4:
        a += ((unsigned int)k[3] << 24);
    /* fallthru */
    case 3:
        a += ((unsigned int)k[2] << 16);
    /* fallthru */
    case 2:
        a += ((unsigned int)k[1] << 8);
    /* fallthru */
    case 1:
        a += k[0];
    }

    __jhash_mix(a, b, c);

    return c;
}

/* A special optimized version that handles 1 or more of unsigned ints.
 * The length parameter here is the number of unsigned ints in the key.
 */
unsigned int jhash2(const unsigned int *k, unsigned int length, unsigned int initval)
{
    unsigned int a, b, c, len;

    a = b = JHASH_GOLDEN_RATIO;
    c = initval;
    len = length;

    while (len >= 3) {
        a += k[0];
        b += k[1];
        c += k[2];
        __jhash_mix(a, b, c);
        k += 3;
        len -= 3;
    }

    c += length * 4;

    switch (len) {
    case 2:
        b += k[1];
    /* fallthru */
    case 1:
        a += k[0];
    }

    __jhash_mix(a, b, c);

    return c;
}

/* A special ultra-optimized versions that knows they are hashing exactly
 * 3, 2 or 1 word(s).
 *
 * NOTE: In partilar the "c += length; __jhash_mix(a,b,c);" normally
 *       done at the end is not done here.
 */
unsigned int jhash_3words(unsigned int a, unsigned int b, unsigned int c, unsigned int initval)
{
    a += JHASH_GOLDEN_RATIO;
    b += JHASH_GOLDEN_RATIO;
    c += initval;

    __jhash_mix(a, b, c);

    return c;
}