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
#define MP_DBG_MODE             (0)

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
#endif
}mem_type_attr_t;

/* -------------------------------- MP FIXED --------------------------------*/

// 预定义固定大小内存空闲链表
pre_declare_list(fixed_free)
// 预定义固定大小内存回收原子队列
pre_declare_spsc_atom_queue(fixed_recycle)

// 固定大小内存节点定义
typedef struct{
    mem_type_attr_t *attr;                  // 所属内存类型
    fixed_free_list_item_t item;            // free list item
    fixed_recycle_spsc_atom_queue_item_t aq_item;     // recycle aq time
    tid_t tid;                              // 内存所属的线程
    unsigned int size;                      // 用户内存大小
    char attr_aligned(8) data[];            // 柔性数组，真正给用户使用的内存区
} attr_aligned(8) fixed_mem_node_t;

// 预定义链表，存储所有回收队列
pre_declare_list(fixed_recycle_aq)

// 回收队列定义，由全局链表组织起来
typedef struct{
    tid_t tid;      // 回收队列所属线程
    fixed_recycle_spsc_atom_queue_head_t local_recycle_aq_head;     // 由本线程回收的内存队列
    fixed_recycle_spsc_atom_queue_head_t remote_recycle_aq_head;    // 由其它线程回收的内存队列
    fixed_recycle_aq_list_item_t item;    // list item
}fixed_recycle_aq_t;

// 预定义哈希表，用于存储所有固定大小空闲链表头
pre_declare_hash(fixed_free_list_head)

// 固定大小空闲内存链表头定义，哈希表存储起来
typedef struct{
    fixed_free_list_head_t  head;   // 空闲链表头
    fixed_recycle_aq_t recycle_aq_head; // 回收队列头
    mem_type_attr_t *attr;          // 所属内存类型
    fixed_free_list_head_hash_item_t item;  // item
}fixed_free_list_t;

/* -------------------------------- MP NONFIXED --------------------------------*/

// 预定义哈希表，用来存储所有的非固定节点内存池
pre_declare_hash(nonfixed_mp)
// 预定义跳表，用来存储空闲的非固定内存节点
pre_declare_skiplist(nonfixed_free)
// 预定义spsc队列，用来存储待回收的非固定内存节点
pre_declare_spsc_atom_queue(nonfixed_recycle)
// 预定义哈希表，用来存储所有线程的非固定内存回收队列，给后台回收线程遍历用
pre_declare_hash(nonfixed_recycle_aq)

// 非固定大小内存节点定义
typedef struct{
    mem_type_attr_t *attr;      // 所属内存类型
    tid_t tid;                      // 逻辑tid
    union{
        nonfixed_free_skiplist_item_t sl_item;          // 跳表item
        nonfixed_recycle_spsc_atom_queue_item_t aq_item;  // 回收队列item
    }item;                          // item，内存节点要么在空闲跳表，要么在回收队列，可使用union压缩
    unsigned char fl;               // 是否位于空闲跳表中，用于判断能否合并
    int prev_size;                  // 前一个内存块的size
    int size;                       // 实际可以用的内存块大小，也就是data部分
    char attr_aligned(8) data[];    // 用户内存
} attr_aligned(8) nonfixed_mem_node_t;

// 非固定大小内存节点回收队列定义
typedef struct{
    tid_t tid;          // 所属线程
    nonfixed_recycle_spsc_atom_queue_head_t local_recycle_aq;   // 需要本线程回收
    nonfixed_recycle_spsc_atom_queue_head_t remote_recycle_aq;  // 需要其他线程回收
    nonfixed_recycle_aq_hash_item_t item;                       // 全局哈希表中的item
}nonfixed_recycle_aq_t;

// 非固定大小内存池定义
typedef struct{
    mem_type_attr_t *attr;                      // 池内节点的内存类型
    nonfixed_free_skiplist_head_t free_sl;      // 空闲跳表头
    nonfixed_recycle_aq_t recycle_aq;               // 待回收队列头
    void *pool_start;
    void *pool_end;                 // 边界
    nonfixed_mp_hash_item_t item;               // 哈希表item
}nonfixed_mp_t;

/* -------------------------------- SYS --------------------------------*/

// extern 全局统计数据
extern ATOMIC_UINT32_T g_mp_calloc_cnt;
extern ATOMIC_UINT64_T g_mp_calloc_size;
extern ATOMIC_UINT32_T g_mp_free_cnt;
extern ATOMIC_UINT64_T g_mp_free_size;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

// 定义固定大小内存空闲链表操作
declare_list(fixed_free, fixed_free, fixed_mem_node_t, item)

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
 * @brief       初始化fixed node mem pool
 * 
 * @param[in]   attr    - mem type attr
 * 
 * @note        查找空闲链表，暂不补充节点数量
 */
extern void _mp_fixed_init(mem_type_attr_t *attr);

/**
 * @brief       supply mem node from system to free list
 * 
 * @param[in]   attr    - mem type attr
 */
extern void fixed_mp_supply(mem_type_attr_t *attr);

/**
 * @brief       get node from fixed mem pool
 * 
 * @param[in]   attr    - mem type
 * 
 * @retval      ptr to buffer
 */
extern void* _mp_fixed_node_get(mem_type_attr_t *attr);

/**
 * @brief       put node to fixed mem pool
 * 
 * @param[in]   ptr     - ptr to buffer
 */
extern void _mp_fixed_node_put(void *ptr);

/**
 * @brief       init nonfixed mem pool
 * 
 * @param[in]   attr    - mem type attr
 */
extern void _mp_nonfixed_init(mem_type_attr_t *attr);

/**
 * @brief       supply nonfixed node into pool
 * 
 * @param[in]   attr    - mem type attr
 * 
 * @note        从系统申请大块内存，加到内存池中
 */
extern void _mp_nonfixed_supply(mem_type_attr_t *attr);

/**
 * @brief       get node from mem pool
 * 
 * @param[in]   attr    - mem type attr
 * @param[in]   size    - user need size
 * 
 * @retval      ptr to node
 */
extern void* _mp_nonfixed_node_get(mem_type_attr_t *attr, int size);

/**
 * @brief       put mem back to nonfixed mp
 * 
 * @param[in]   ptr - ptr to node
 */
extern void _mp_nonfixed_node_put(void *ptr);

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
static attr_force_inline void _mem_type_attr_ ## _name ## _init() attr_ctor(CTOR_PRIO_LOW);  \
static inline void _mem_type_attr_ ## _name ## _init()   \
{   \
    mem_type_attr_init(&_mem_type_attr_ ## _name);    \
}   \
/* _declare_mem_type_attr end */

/**
 * 外部使用，声明一类固定大小的内存类型
 */
#define declare_mem_type_fixed(name, node_size, node_max_num)   \
    _declare_mem_type_attr(name, MEM_TYPE_ATTR_FIXED_SIZE, node_size, node_max_num) \
/* declare_mem_type_fixed end */

/**
 * 外部使用，初始化一个固定节点大小内存池
 * 0. 初始化全局空闲链表
 * 1. 在【线程私有】的全局空闲链表头哈希表中查找有无空闲链表
 *  1.1 无的话，创建并加入哈希表
 * 2. 创建aq用于跨线程回收
*/
#define mp_fixed_init(name) \
    _mp_fixed_init(&_mem_type_attr_ ## name);   \
/* declare_mp_fixed */

/**
 * 外部使用，补充空闲节点
 */
#define mp_fixed_supply(name)   \
    fixed_mp_supply(&_mem_type_attr_ ## name);    \
/* mp_fixed_supply end */

/**
 * 外部使用，从内存池获取一个固定大小的内存结点
 */
#define mp_fixed_node_get(name) \
    _mp_fixed_node_get(&_mem_type_attr_ ## name);   \
/* mp_fixed_node_get end */

/**
 * 外部使用，归还一个固定大小内存节点给内存池
 */
#define mp_fixed_node_put(ptr)  \
    _mp_fixed_node_put(ptr);    \

/**
 * 外部使用，声明不定长内存类型，对于同个name，全局调用一次
 */
#define declare_mem_type_nonfixed(name) \
    _declare_mem_type_attr(name, 0, 0, 0)   \
/* declare_mem_type_nonfixed end */

/**
 * 外部使用，声明不定长内存类型
 */
#define declare_mem_type_nonfixed_extern(name) \
    extern mem_type_attr_t _mem_type_attr_ ## name; \
/* declare_mem_type_nonfixed_extern end */

/**
 * 外部使用，初始化非固定大小内存池
 */
#define mp_nonfixed_init(name)  \
    _mp_nonfixed_init(&_mem_type_attr_ ## name);    \
/* mp_nonfixed_init end */

/**
 * 外部使用，补充非固定大小内存节点
 */
#define mp_nonfixed_supply(name)    \
    _mp_nonfixed_supply(&_mem_type_attr_ ## name);  \
/* mp_nonfixed_supply end */

/**
 * 外部使用，申请非固定大小内存
 */
#define mp_nonfixed_node_get(name, size)    \
    _mp_nonfixed_node_get(&_mem_type_attr_ ## name, size);  \
/* mp_nonfixed_node_get end */

/**
 * 外部使用，返还内存回池子
 */
#define mp_nonfixed_node_put(ptr)   \
    _mp_nonfixed_node_put(ptr); \
/* mp_nonfixed_node_put end */

/* ========================================================================== */
/*                              Debug Function                                */
/* ========================================================================== */

#define MP_TRACE    (1)

#if MP_TRACE

#include <stdio.h>

/**
 * @brief       dump all fixed free list info
 */
extern void mp_dump_fixed_free_list();

/**
 * @brief       dump all nonfixed mp
 * 
 * @note        调试函数，打印非固定大小内存池跳表。需要在没有内存分配和回收时使用
 */
extern void nonfixed_mp_dump();

#else

#define mp_dump_fixed_free_list()   
#define nonfixed_mp_dump()  

#endif

#endif
/* __MP_H__ end*/