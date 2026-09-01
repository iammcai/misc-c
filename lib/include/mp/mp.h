/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    mp.h
 * @brief   内存池头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-05-27
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-05-27 | cai | Initial creation.
 *   1.1 | 2026-06-14 | cai | Amend init, add mp_fixed_supply.
 *   1.2 | 2026-06-15 | cai | Add debug method.
 *   1.3 | 2026-07-11 | cai | Add auto init mp.
 *   2.0 | 2026-07-14 | cai | Add nonfixed mp.
 *   3.0 | 2026-07-21 | cai | Add mp bin.
 *   4.0 | 2026-08-27 | cai | Remove mp_fixed to mp_fixed.h
 */

#ifndef __MP_H__
#define __MP_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <assert.h>
#include <string.h>
#include "type/type_list.h"
#include "type/type_hash.h"
#include "type/type_atom_queue.h"
#include "type/type_skiplist.h"
#include "mp/mp_base.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 表示是否开启调试，开启时会添加计数等，导致性能降低
#define MP_DBG_MODE             (1)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 逻辑tid类型
typedef unsigned short tid_t;

// 预定义哈希表存储内存类型属性
pre_declare_hash(mem_type_attr)

// 内存属性枚举
typedef enum{
    MEM_TYPE_ATTR_FIXED_SIZE = 1 << 0,      // 节点大小固定
    MEM_TYPE_ATTR_SLAB = 1 << 1,            // 分级内存
    MEM_TYPE_ATTR_DYNAMIC_SUPPLY = 1 << 2,  // 动态补充
}mem_type_attr_flag_e;

// 内存类型属性
typedef struct{
    const char *name;                   // 类型名
    unsigned int flag;                  // 标志位
    unsigned int node_size;             // 该类型的内存节点大小
    unsigned int node_max_num;          // 该类型的最大节点数量
    unsigned int slab_node_cnt[SLAB_SIZE_CNT];      // slab使用,各个类型大小节点的数量
    mem_type_attr_hash_item_t item;     // hash item

    ATOMIC_UINT32_T allocated;          // 总共申请的数量
#if MP_DBG_MODE
    ATOMIC_UINT32_T used;               // 正在使用的数量
    ATOMIC_UINT32_T piece;              // 碎片数量，for nonfixed
    /* -------- slab使用 -------- */
    ATOMIC_UINT32_T slab_cnt[SLAB_SIZE_CNT];    // 每个链表最大数量
    ATOMIC_UINT32_T slab_used[SLAB_SIZE_CNT];   // 每个链表分配出去的数量
    ATOMIC_UINT32_T hit_total;                  // 命中总次数
    ATOMIC_UINT32_T slab_hit[SLAB_SIZE_CNT];    // 各个slab命中次数
    unsigned int slab_full_times[SLAB_SIZE_CNT];        // 告警次数
#endif
}mem_type_attr_t;

/* -------------------------------- SYS --------------------------------*/

// extern 全局统计数据
extern ATOMIC_UINT32_T g_mp_calloc_cnt;
extern ATOMIC_UINT64_T g_mp_calloc_size;
extern ATOMIC_UINT32_T g_mp_free_cnt;
extern ATOMIC_UINT64_T g_mp_free_size;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       init mem type attr module
 * 
 * @note        从mp_module_init分离出来
 */
extern void mem_type_attr_module_init();

/**
 * @brief       init mp module
 */
extern void mp_module_init();

/**
 * @brief       封装hash add，加到全局哈希表中
 * 
 * @param[in]   attr    - mem type attr
 */
extern void mem_type_attr_init(mem_type_attr_t *attr);

/**
 * @brief       find mem type attr in global hashtable
 * 
 * @param[in]   name    - attr name
 * 
 * @retval      ptr to mem type attr
 */
extern mem_type_attr_t* mem_type_attr_find(const char *name);

/**
 * @brief       get first attr in mem attr hashtable
 */
extern mem_type_attr_t* mem_type_attr_first();

/**
 * @brief       get next attr in mem attr hashtable
 */
extern mem_type_attr_t* mem_type_attr_next(mem_type_attr_t *attr);

/**
 * @brief       check if attr slab
 * 
 * @param[in]   attr    - mem type attr
 * 
 * @retval      0 - no, else - yes
 */
extern unsigned int mem_type_attr_slab(mem_type_attr_t *attr);

/**
 * @brief       new a logic tid
 * 
 * @note        分配一个新的逻辑ID，原子ADD保证并发可用
 */
extern tid_t tid_new();

/**
 * @brief       call system calloc
 * 
 * @param[in]   num     - element num
 * @param[in]   size    - elememt size
 * 
 * @retval      ptr to memory
 */
static attr_force_inline void* mp_calloc(size_t num, size_t size)
{
    void* ret = calloc(num, size);
    assert(ret);

    ATOM_FETCH_ADD(&g_mp_calloc_cnt, 1, MORDER_ACQ_REL);
    ATOM_FETCH_ADD(&g_mp_calloc_size, num*size, MORDER_ACQ_REL);

    return ret;
}

/**
 * @brief       call system realloc
 * 
 * @param[in]   ptr - ptr to buffer
 * @param[in]   size - new size
 * @param[in]   ori_size    - origin size
 * 
 * @retval      new ptr to buffer
 */
static attr_force_inline void* mp_realloc(void *ptr, size_t size, size_t ori_size)
{
    void *ret = realloc(ptr, size);
    assert(ret);

    ATOM_FETCH_ADD(&g_mp_free_cnt, 1, MORDER_ACQ_REL);
    ATOM_FETCH_ADD(&g_mp_free_size, ori_size, MORDER_ACQ_REL);
    ATOM_FETCH_ADD(&g_mp_calloc_cnt, 1, MORDER_ACQ_REL);
    ATOM_FETCH_ADD(&g_mp_calloc_size, size, MORDER_ACQ_REL);

    return ret;
}

/**
 * @brief       call system calloc, memcpy for strdup
 * 
 * @param[in]   str     - string to dup
 * 
 * @retval      dup string
 */
static attr_force_inline void* mp_strdup(const char *str)
{
    if(!str)
        return NULL;
    char *dup = mp_calloc(strlen(str)+1, sizeof(char));
    memcpy(dup, str, strlen(str));
    return dup;
}

/**
 * @brief       call system free
 * 
 * @param[in]   ptr     - ptr to buffer
 * @param[in]   size    - size of buffer
 * 
 * @note        size依赖用户输入，务必别自欺欺人
 */
static attr_force_inline void mp_free(void *ptr, unsigned int size)
{
    if(ptr)
    {
        free(ptr);

        ATOM_FETCH_ADD(&g_mp_free_cnt, 1, MORDER_ACQ_REL);
        ATOM_FETCH_ADD(&g_mp_free_size, size, MORDER_ACQ_REL);
    }
}

/**
 * @brief       register mem type attr
 * 
 * @param[in]   attr
 */
static void _mem_type_attr_register(mem_type_attr_t *attr)
{
    mem_type_attr_init(attr);
}

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 内部使用，定义内存类型，定义变量并构造加入hash
#define _declare_mem_type_attr(_name, _flag, _node_size, _node_max_num)  \
mem_type_attr_t _mem_type_attr_ ## _name = {    \
    .name = #_name, \
    .flag = _flag,  \
    .node_size = _node_size,    \
    .node_max_num = _node_max_num   \
};  \
/* _declare_mem_type_attr end */

/**
 * 外部使用，注册内存类型
 */
#define mem_type_attr_register(_name)   \
    _mem_type_attr_register(&_mem_type_attr_ ## _name); \
/* mem_type_attr_register end */

/**
 * 外部使用，声明一类固定大小的内存类型
 */
#define declare_mem_type_fixed(name, node_size, node_max_num)   \
    _declare_mem_type_attr(name, MEM_TYPE_ATTR_FIXED_SIZE, node_size, node_max_num) \
/* declare_mem_type_fixed end */

/* ========================================================================== */
/*                              Debug Function                                */
/* ========================================================================== */

#endif
/* __MP_H__ end*/