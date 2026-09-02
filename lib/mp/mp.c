/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    mp.c
 * @brief   内存池实现
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
 *   3.0 | 2026-08-27 | cai | Remove mp_fixed to mp_fixed.c
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "mp/mp.h"
#include "plat/atom.h"
#include "event/ev_thread.h"
#include "cli/cli.h"
#include "event/ev_lock.h"
#include "syslog/syslog.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define RECYCLE_WAKE_THRESHOLD  (64)   // 唤醒阈值

/* ========================================================================== */
/*                           Static Function Prototypes                       */
/* ========================================================================== */

/**
 * @brief       mem type attr cmp func
 * 
 * @param[in]   a1  - attr 1
 * @param[in]   a2  - attr 2
 * 
 * @retval      cmp val
 * 
 * @note        cmp by name
 */
static attr_pure_inline int _mem_type_attr_cmp(mem_type_attr_t *a1, mem_type_attr_t *a2)
{
    assert(a1 && a2 && a1->name && a2->name);
    return strcmp(a1->name, a2->name);
}

/**
 * @brief       mem type attr hash func
 * 
 * @param[in]   a   - attr
 * 
 * @retval      hash val
 * 
 * @note        hash by name
 */
static attr_pure_inline unsigned int _mem_type_attr_hash(mem_type_attr_t *a)
{
    assert(a && a->name);
    return type_hash_jhash(a->name, strlen(a->name), 0);
}

/**
 * @brief       check if attr fixed_size
 * 
 * @param[in]   attr    - mem type attr
 * 
 * @retval      0 - no, else - yes
 */
static attr_pure_inline unsigned int mem_type_attr_fixed_size(const mem_type_attr_t *attr)
{
    return attr->flag & MEM_TYPE_ATTR_FIXED_SIZE;
}

extern unsigned int mem_type_attr_slab(const mem_type_attr_t *attr)
{
    return attr->flag & MEM_TYPE_ATTR_SLAB;
}

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

// 全局哈希表，存储所有内存类型属性
static mem_type_attr_hash_head_t g_mem_type_attr_hash_head = {};
// 全局哈希表的锁
static ev_rwlock_t g_mem_type_attr_hash_rwlock = EV_RWLOCK_INITIALIZER;

// 全局tid，用于自行管理分配逻辑线程ID
static tid_t g_tid = 0;

// 全局统计数据
ATOMIC_UINT32_T g_mp_calloc_cnt = 0;     // 申请次数
ATOMIC_UINT64_T g_mp_calloc_size = 0;    // 申请大小
ATOMIC_UINT32_T g_mp_free_cnt = 0;       // 释放次数
ATOMIC_UINT64_T g_mp_free_size = 0;      // 释放大小

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

// 定义内存类型属性哈希表相关操作
declare_hash(mem_type_attr, mem_type_attr, mem_type_attr_t, item, 1, 31, _mem_type_attr_cmp, _mem_type_attr_hash)

void mem_type_attr_init(mem_type_attr_t *attr)
{
    assert(attr && attr->name);

    ATOM_STORE(&attr->allocated, 0, MORDER_RELEASE);
#if MP_DBG_MODE
    ATOM_STORE(&attr->used, 0, MORDER_RELEASE);
#endif

    ev_with_wrlock(&g_mem_type_attr_hash_rwlock)
        mem_type_attr_hash_add(&g_mem_type_attr_hash_head, attr);
}

mem_type_attr_t* mem_type_attr_find(const char *name)
{
    if(!name)
        return NULL;
    mem_type_attr_t key = {.name = name};
    mem_type_attr_t *result = NULL;

    ev_with_rdlock(&g_mem_type_attr_hash_rwlock)
        result = mem_type_attr_hash_find(&g_mem_type_attr_hash_head, &key);

    return result;
}

mem_type_attr_t* mem_type_attr_first()
{
    mem_type_attr_t *result = NULL;
    ev_with_rdlock(&g_mem_type_attr_hash_rwlock)
        result = mem_type_attr_hash_first(&g_mem_type_attr_hash_head);
    return result;
}

mem_type_attr_t* mem_type_attr_next(mem_type_attr_t *attr)
{
    mem_type_attr_t *result = NULL;
    ev_with_rdlock(&g_mem_type_attr_hash_rwlock)
        result = mem_type_attr_hash_next(&g_mem_type_attr_hash_head, attr);
    return result;
}

tid_t tid_new()
{
    return ATOM_ADD_FETCH(&g_tid, 1, MORDER_ACQ_REL);
}

void mem_type_attr_module_init()
{
    // attr哈希表初始化
    mem_type_attr_hash_init(&g_mem_type_attr_hash_head);

    dbg_major("mem type attr for mp init ok");
}

void mp_module_init()
{
    dbg_major("mp module init ok");
}

/* ========================================================================== */
/*                              Debug Function                                */
/* ========================================================================== */