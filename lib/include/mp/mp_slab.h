/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    mp_slab.h
 * @brief   分级内存池头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-21
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-21 | cai | Initial creation.
 */

#ifndef __MP_SLAB_H__
#define __MP_SLAB_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "mp/mp_base.h"
#include "mp/mp.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 预定义链表，用来组织空闲节点
pre_declare_list(slab_free_mem)
// 预定义哈希表，用来组织线程独立slab内存池
pre_declare_hash(slab_mp)
// 预定义hashtable，用来组织回收队列
pre_declare_hash(slab_recycle_aq)
// 预定义回收spsc队列
pre_declare_spsc_atom_queue(slab_recycle)

// slab内存节点定义
typedef struct{
    uint32_t magic;             // 魔数
    mem_type_attr_t *attr;      // 所属内存池
    tid_t tid;                  // 所属线程
    union{
        slab_free_mem_list_item_t fl_item;      // 空闲链表item
        slab_recycle_spsc_atom_queue_item_t aq_item;    // 回收队列item
    }item;
    uint8_t slot;   // 所处的槽位
} attr_aligned(8) slab_mem_node_t;

// 跨线程回收队列定义
typedef struct{
    tid_t tid;      // 所属线程
    slab_recycle_spsc_atom_queue_head_t local_aq;   // 本地回收队列
    slab_recycle_spsc_atom_queue_head_t remote_aq;  // 远端回收队列
    slab_recycle_aq_hash_item_t item;  // 哈希表item
}slab_recycle_aq_t;

// slab内存池定义
typedef struct{
    mem_type_attr_t *attr;      // 所属内存类型
    slab_free_mem_list_head_t free_lists[SLAB_SIZE_CNT];        // 空闲链表数组
    slab_recycle_aq_t recycle;      // recycle相关信息
    slab_mp_hash_item_t item;                   // 哈希表item
}slab_mp_t;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/**
 * 外部使用，声明slab内存类型，全局仅可以使用一次
 */
#define declare_mem_type_slab(_name, cnt8, cnt16, cnt32, cnt64, cnt128, cnt256, cnt512, cnt1024, cnt2048, cnt4096, cnt9216) \
    _declare_mem_type_attr(_name, MEM_TYPE_ATTR_SLAB, 0, 0);    \
static void _name ## _slab_cnt_set() attr_ctor(CTOR_PRIO_MID);  \
static void _name ## _slab_cnt_set()    \
{   \
    _mem_type_attr_ ## _name.slab_node_cnt[SLAB_SIZE_8] = cnt8;         \
    _mem_type_attr_ ## _name.slab_node_cnt[SLAB_SIZE_16] = cnt16;       \
    _mem_type_attr_ ## _name.slab_node_cnt[SLAB_SIZE_32] = cnt32;       \
    _mem_type_attr_ ## _name.slab_node_cnt[SLAB_SIZE_64] = cnt64;       \
    _mem_type_attr_ ## _name.slab_node_cnt[SLAB_SIZE_128] = cnt128;     \
    _mem_type_attr_ ## _name.slab_node_cnt[SLAB_SIZE_256] = cnt256;     \
    _mem_type_attr_ ## _name.slab_node_cnt[SLAB_SIZE_512] = cnt512;     \
    _mem_type_attr_ ## _name.slab_node_cnt[SLAB_SIZE_1024] = cnt1024;   \
    _mem_type_attr_ ## _name.slab_node_cnt[SLAB_SIZE_2048] = cnt2048;   \
    _mem_type_attr_ ## _name.slab_node_cnt[SLAB_SIZE_4096] = cnt4096;   \
    _mem_type_attr_ ## _name.slab_node_cnt[SLAB_SIZE_9216] = cnt9216;   \
}   \
/* declare_mem_type_slab end */

/**
 * 外部使用，声明slab内存类型，extern
 */
#define declare_mem_type_slab_extern(_name) \
    extern mem_type_attr_t _mem_type_attr_ ## _name;    \
/* declare_mem_type_slab_extern end */

/**
 * 外部使用，初始化slab内存池
 */
#define mp_slab_init(_name)  \
    _mp_slab_init(&_mem_type_attr_ ## _name);   \
/* declare_slab_mp end */

/**
 * 外部使用，补充slab内存
 */
#define mp_slab_supply(_name)   \
    _mp_slab_supply(&_mem_type_attr_ ## _name); \
/* mp_slab_supply end*/

/**
 * 外部使用，申请slab内存
 */
#define mp_slab_node_get(_name, _size)  \
    _mp_slab_node_get(&_mem_type_attr_ ## _name, _size);    \
/* mp_slab_node_get end */

/**
 * 外部使用，释放slab内存
 */
#define mp_slab_node_put(ptr)   \
    _mp_slab_node_put(ptr); \
/* mp_slab_node_put end */

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       int mp slab module
 */
extern void mp_slab_module_init();

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

/**
 * @brief       init slab memory pool
 * 
 * @param[in]   attr
 * 
 * @note        初始化slab mp
 */
extern void _mp_slab_init(mem_type_attr_t *attr);

/**
 * @brief       supply mem node into slab mp
 * 
 * @param[in]   attr
 * 
 * @note        补充slab内存节点
 */
extern void _mp_slab_supply(mem_type_attr_t *attr);

/**
 * @brief       get mem node from slab
 * 
 * @param[in]   attr
 * @param[in]   size
 * 
 * @retval      mem node
 * 
 * @note        申请内存，支持懒初始化
 */
extern void* _mp_slab_node_get(mem_type_attr_t *attr, int size);

/**
 * @brief       put mem node back to slab mp
 * 
 * @param[in]   ptr     - pointer to mem
 */
extern void _mp_slab_node_put(void *ptr);

/**
 * @brief       cli hook for debug show mp slab
 */
extern void mp_slab_show_cli_hook(unsigned char argc, char *argv[]);

#endif
/* __MP_SLAB_H__ end */