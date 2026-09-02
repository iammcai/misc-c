/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    mp_nonfixed.c
 * @brief   非固定大小内存池实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-01
 * @version 1.0
 *
 * @note    重构mp.c原来关于nonfixed_mp的实现
 *
 * @history
 *   1.0 | 2026-09-01 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <unistd.h>
#include <stdio.h>

#include "mp/mp.h"
#include "mp/mp_nonfixed.h"
#include "plat/debug.h"
#include "event/ev_lock.h"
#include "type/type_skiplist.h"
#include "event/ev_thread.h"
#include "syslog/syslog.h"
#if MP_NONFIXED_DBG
#include "cli/cli.h"
#endif

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 分割阈值
#define MP_NONFIXED_NODE_SIZE_MIN       (64)

// node -> data
#define mp_nonfixed_node_data(node)     node->data
// data -> node
#define mp_nonfixed_data_node(_data)    container_of(_data, mp_nonfixed_node_t, data)

// 新分配tid
#define tid_new()           ATOM_FETCH_ADD(&g_tid, 1, MORDER_ACQ_REL)

// 后台回收线程执行间隔
#define MP_NONFIXED_RECYCLE_THD_WORK_INTERVAL   (1000)

#if MP_NONFIXED_DBG

// attr打印格式：name, size, piece_cnt, free_size, left
#define MP_NONFIXED_ATTR_FMT_HEAD       "%-20s%-16s%-16s%-16s%-10s\n"
// attr data打印格式
#define MP_NONFIXED_ATTR_FMT_DATA       "%-20s%-16u%-16u%-16u%-10.3f\n"

// aq打印格式，name, tid, local, remote, max_node_size
#define MP_NONFIXED_RECYCLE_AQ_FMT_HEAD    "%-20s%-8s%-8s%-8s%-16s\n"
// aq打印数据格式
#define MP_NONFIXED_RECYCLE_AQ_FMT_DATA    "%-20s%-8d%-8d%-8d%-16d\n"

#endif

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 逻辑tid
typedef unsigned short tid_t;

// 预定义skiplist，用于空闲节点，按照size排序
pre_declare_skiplist(mp_nonfixed_node)

// 预定义spsc_aq，用于回收节点
pre_declare_spsc_atom_queue(mp_nonfixed_recycle)

// 预定义skiplist，用于空闲节点，按照addr排序，全量合并用
pre_declare_skiplist(mp_nonfixed_node_addr)

// 非固定大小内存节点定义
typedef struct{
    mem_type_attr_nonfixed_t *attr;     // 隶属内存类型
    uint32_t size;                      // 节点用户内存大小
    tid_t tid;              // 分配来源线程
    uint8_t used;           // 分配标记，1表示给用户
    union{
        mp_nonfixed_node_skiplist_item_t sl_item;               // skiplist item
        mp_nonfixed_recycle_spsc_atom_queue_item_t aq_item;     // atom queue item
    }item;
    mp_nonfixed_node_addr_skiplist_item_t addr_item;            // skiplist item, sort by addr
    char attr_aligned(8) data[];        // 用户内存
} attr_aligned(8) mp_nonfixed_node_t;

// 预定义hash，用于全局管理所有线程回收结构
pre_declare_hash(mp_nonfixed_recycle)

// 回收结构定义
typedef struct{
    tid_t tid;
    mp_nonfixed_recycle_spsc_atom_queue_head_t local;       // 来自本线程
    mp_nonfixed_recycle_spsc_atom_queue_head_t remote;      // 来自其它线程

    mp_nonfixed_recycle_hash_item_t item;       // hash item
}mp_nonfixed_recycle_t;

// 预定义hash，用于管理线程独立的所有内存池
pre_declare_hash(nonfixed_mp)

// 非固定大小内存池定义
typedef struct{
    mem_type_attr_nonfixed_t *attr;                 // 内存类型
    mp_nonfixed_node_skiplist_head_t free_sl;       // 空闲跳表
    mp_nonfixed_node_addr_skiplist_head_t addr_sl;  // 空闲跳表，按照addr排序
    mp_nonfixed_recycle_t recycle;                  // 回收相关
    
    uintptr_t mem_chunk_end;                        // 内存边界

    nonfixed_mp_hash_item_t item;       // hash item
}nonfixed_mp_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       cmp for sl
 */
static attr_pure_inline int _mp_nonfixed_node_cmp(const mp_nonfixed_node_t *node1, const mp_nonfixed_node_t *node2)
{
    if(node1->size < node2->size)
        return -1;
    else if(node1->size == node2->size)
        return 0;
    return 1;
}

// 定义sl操作，用于空闲节点
declare_skiplist(mp_nonfixed_node, mp_nonfixed_node, mp_nonfixed_node_t, item.sl_item, _mp_nonfixed_node_cmp)

/**
 * @brief       cmp for sl
 */
static attr_pure_inline int _mp_nonfixed_node_cmp_by_addr(const mp_nonfixed_node_t *node1, const mp_nonfixed_node_t *node2)
{
    if(node1 < node2)
        return -1;
    else if(node1 == node2)
        return 0;
    return 1;
}

// 定义sl操作，用于空闲节点，按照addr排序
declare_skiplist(mp_nonfixed_node_addr, mp_nonfixed_node_addr, mp_nonfixed_node_t, addr_item, _mp_nonfixed_node_cmp_by_addr)

/**
 * @brief       cmp for hash
 */
static attr_pure_inline int _mem_type_attr_nonfixed_cmp(const mem_type_attr_nonfixed_t *attr1, const mem_type_attr_nonfixed_t *attr2)
{
    return strcmp(attr1->name, attr2->name);
}

/**
 * @brief       hash for hashtable
 */
static attr_pure_inline unsigned int _mem_type_attr_nonfixed_hash(const mem_type_attr_nonfixed_t *attr)
{
    return type_hash_jhash(attr->name, strlen(attr->name), 0);
}

// 定义hash操作，用于管理mem type
declare_hash(mem_type_attr_nonfixed, mem_type_attr_nonfixed, mem_type_attr_nonfixed_t, item, 1, 31, _mem_type_attr_nonfixed_cmp, _mem_type_attr_nonfixed_hash)

/**
 * @brief       cmp for hash
 */
static attr_pure_inline int _nonfixed_mp_cmp(const nonfixed_mp_t *mp1, const nonfixed_mp_t *mp2)
{
    return strcmp(mp1->attr->name, mp2->attr->name);
}

/**
 * @brief       hash for hashtable
 */
static attr_pure_inline unsigned int _nonfixed_mp_hash(const nonfixed_mp_t *mp)
{
    return type_hash_jhash(mp->attr->name, strlen(mp->attr->name), 0);
}

// 定义hash操作，用于管理nonfixed_mp
declare_hash(nonfixed_mp, nonfixed_mp, nonfixed_mp_t, item, 1, 31, _nonfixed_mp_cmp, _nonfixed_mp_hash)

// 定义spsc_aq操作，用于内存回收队列
declare_spsc_atom_queue(mp_nonfixed_recycle, mp_nonfixed_recycle, mp_nonfixed_node_t, item.aq_item)

/**
 * @brief       cmp for hash
 */
static attr_pure_inline int _mp_nonfixed_recycle_cmp(const mp_nonfixed_recycle_t *re1, const mp_nonfixed_recycle_t *re2)
{
    return (int)re1->tid - re2->tid;
}

/**
 * @brief       hash for hashtable
 */
static attr_pure_inline unsigned int _mp_nonfixed_recycle_hash(mp_nonfixed_recycle_t *re)
{
    return type_hash_jhash(&re->tid, sizeof(tid_t), 0);
}

// 定义hash操作，用于管理线程内存回收结构
declare_hash(mp_nonfixed_recycle, mp_nonfixed_recycle, mp_nonfixed_recycle_t, item, 1, 31, _mp_nonfixed_recycle_cmp, _mp_nonfixed_recycle_hash)

/**
 * @brief       work function of recycle ev thd
 */
static void _mp_nonfixed_recycle_thd_wf(void *args);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 全局mem type hashtable
static mem_type_attr_nonfixed_hash_head_t g_mem_type_attr_nonfixed_hashtable = {};
// lock
static ev_rwlock_t g_mem_type_attr_nonfixed_hashtable_rwlock = EV_RWLOCK_INITIALIZER;

// 线程独立mp hashtable，无锁
static thread_local nonfixed_mp_hash_head_t g_nonfixed_mp_hashtable = {};

// 线程独立attr cache
static thread_local mem_type_attr_nonfixed_t *g_attr_cache = NULL;
// 线程独立mp cache
static thread_local nonfixed_mp_t *g_mp_cache = NULL;

// 全局tid
static tid_t g_tid = 0;

// 全局recycle hash
static mp_nonfixed_recycle_hash_head_t g_mp_nonfixed_recycle_hashtable = {};
// lock
static ev_rwlock_t g_mp_nonfixed_recycle_hashtable_rwlock = EV_RWLOCK_INITIALIZER;

// 后台回收线程
declare_ev_thd(_mp_nonfixed_recycle_thd, _mp_nonfixed_recycle_thd_wf, NULL, MP_NONFIXED_RECYCLE_THD_WORK_INTERVAL)

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

/**
 * @brief       split node, new node back to free sl
 * 
 * @param[in]   mp
 * @param[in]   node
 * @param[in]   size    - 节点申请的内存大小
 */
static void _mp_nonfixed_node_split(nonfixed_mp_t *mp, mp_nonfixed_node_t *node, uint32_t size)
{
    // 若差距过大，进行分割
    // 例如当前node->size == 128B
    // size == 16B
    // 剩余102B可以分出新的节点，new_node->size = 128 - 16 - sizeof(mp_nonfixed_node_t)
    if(node->size >= (size + sizeof(mp_nonfixed_node_t) + MP_NONFIXED_NODE_SIZE_MIN))
    {
        mp_nonfixed_node_t *new_node = (mp_nonfixed_node_t*)(node->data + size);        // 新节点的起始
        memset(new_node, 0, sizeof(mp_nonfixed_node_t));
        new_node->attr = node->attr;
        new_node->tid = node->tid;
        new_node->size = node->size - size - sizeof(mp_nonfixed_node_t);
        new_node->used = 0;
        mp_nonfixed_node_skiplist_add(&mp->free_sl, new_node);      // 加回到空闲跳表
        mp_nonfixed_node_addr_skiplist_add(&mp->addr_sl, new_node);
#if MP_NONFIXED_DBG
        ATOM_FETCH_ADD(&node->attr->piece_cnt, 1, MORDER_RELAXED);
        ATOM_FETCH_ADD(&node->attr->free_size, new_node->size, MORDER_RELAXED);     // 加回来一部分，其中使用一部分free mem用来作head
#endif
        // 修改原来node->size
        node->size = size;
    }
}

/**
 * @brief           get node fast path
 * 
 * @param[in]       attr
 * @param[in]       size
 * 
 * @retval          mem
 */
static attr_force_inline void* _mp_nonfixed_node_get_fast(mem_type_attr_nonfixed_t *attr, uint32_t size)
{
    if(unlikely(attr != g_attr_cache))
        return NULL;

    mp_nonfixed_node_t key = {.size = size};
    mp_nonfixed_node_t *node = mp_nonfixed_node_skiplist_ceil(&g_mp_cache->free_sl, &key);

    if(likely(node))
    {
        // 获取节点，从跳表移除
        mp_nonfixed_node_skiplist_del(&g_mp_cache->free_sl, node);
        mp_nonfixed_node_addr_skiplist_del(&g_mp_cache->addr_sl, node);
#if MP_NONFIXED_DBG
        ATOM_FETCH_SUB(&attr->piece_cnt, 1, MORDER_RELAXED);
        ATOM_FETCH_SUB(&attr->free_size, node->size, MORDER_RELAXED);
#endif
        // 节点分割
        _mp_nonfixed_node_split(g_mp_cache, node, size);
        node->used = 1;
        return mp_nonfixed_node_data(node);
    }

    return NULL;
}

/**
 * @brief           supply node into mp from sys
 * 
 * @param[in]       mp  - nonfixed mp
 */
static void _mp_nonfixed_supply(nonfixed_mp_t *mp)
{
    mp_nonfixed_node_t *node = mp_calloc(1, sizeof(mp_nonfixed_node_t) + mp->attr->size);
    node->attr = mp->attr;
    node->size = mp->attr->size;
    node->tid = mp->recycle.tid;
    node->used = 0;
    mp->mem_chunk_end = (uintptr_t)(void*)(node->data + mp->attr->size);
    mp_nonfixed_node_skiplist_add(&mp->free_sl, node);
    mp_nonfixed_node_addr_skiplist_add(&mp->addr_sl, node);
#if MP_NONFIXED_DBG
    ATOM_FETCH_ADD(&mp->attr->piece_cnt, 1, MORDER_RELAXED);
    ATOM_FETCH_ADD(&mp->attr->free_size, mp->attr->size, MORDER_RELAXED);
#endif
}

/**
 * @brief       find or create nonfixed mp
 * 
 * @param[in]   attr
 * @param[in]   supply  - if create mp, supply node or not
 * 
 * @retval      mp
 */
static nonfixed_mp_t* _nonfixed_mp_find_or_create(mem_type_attr_nonfixed_t *attr, int supply)
{
    // 检查线程的mp hash是否初始化
    nonfixed_mp_hash_head_t empty = {};
    if(unlikely(!memcmp(&empty, &g_nonfixed_mp_hashtable, sizeof(nonfixed_mp_hash_head_t))))
        nonfixed_mp_hash_init(&g_nonfixed_mp_hashtable);

    // 查找mp
    nonfixed_mp_t key = {.attr = attr};
    nonfixed_mp_t *mp = nonfixed_mp_hash_find(&g_nonfixed_mp_hashtable, &key);
    if(mp)
        goto done;

    // 查找mp失败，需要创建新的加入
    mp = mp_calloc(1, sizeof(nonfixed_mp_t));
    mp->attr = attr;

    mp->recycle.tid = tid_new();
    mp_nonfixed_recycle_spsc_atom_queue_init(&mp->recycle.local);
    mp_nonfixed_recycle_spsc_atom_queue_init(&mp->recycle.remote);
    ev_with_wrlock(&g_mp_nonfixed_recycle_hashtable_rwlock)
        mp_nonfixed_recycle_hash_add(&g_mp_nonfixed_recycle_hashtable, &mp->recycle);

    mp_nonfixed_node_skiplist_init(&mp->free_sl);
    
    if(supply)
        _mp_nonfixed_supply(mp);
    nonfixed_mp_hash_add(&g_nonfixed_mp_hashtable, mp);

done:
    g_mp_cache = mp;
    g_attr_cache = attr;
    return mp;
}

/**
 * @brief       merge free node，avoid piece
 * 
 * @param[in]   mp
 * @param[in]   node
 * 
 * @retval      result - 合并后的节点
 * 
 * @note        空闲节点合并，避免外部碎片过多导致分配失败。合并方向往后
 */
static mp_nonfixed_node_t* _mp_nonfixed_node_merge(nonfixed_mp_t *mp, mp_nonfixed_node_t *node)
{
    mp_nonfixed_node_t *next_node = (mp_nonfixed_node_t*)(node->data + node->size);
    if(unlikely((uintptr_t)next_node >= mp->mem_chunk_end))     // 最后一个节点不处理
        return node;

    if(next_node->used == 0)
    {
        // 先移出跳表
        mp_nonfixed_node_skiplist_del(&mp->free_sl, next_node);
        mp_nonfixed_node_addr_skiplist_del(&mp->addr_sl, next_node);
#if MP_NONFIXED_DBG
    ATOM_FETCH_SUB(&mp->attr->piece_cnt, 1, MORDER_RELAXED);
    ATOM_FETCH_SUB(&mp->attr->free_size, next_node->size, MORDER_RELAXED);
#endif
        // 进行节点合并
        node->size += (sizeof(mp_nonfixed_node_t) + next_node->size);
    }

    return node;
}

/**
 * @brief       full merged free sl
 * 
 * @param[in]   mp  - mp
 * 
 * @note        全量合并碎片，实在找不到空闲内存时触发
 */
static void _mp_nonfixed_free_sl_full_merged(nonfixed_mp_t *mp)
{
    mp_nonfixed_node_addr_skiplist_head_t addr_sl_new = {};
    mp_nonfixed_node_addr_skiplist_init(&addr_sl_new);

    int before = mp_nonfixed_node_skiplist_count(&mp->free_sl);

    mp_nonfixed_node_skiplist_fini(&mp->free_sl);
    while(mp_nonfixed_node_addr_skiplist_count(&mp->addr_sl))
    {
        // pop一个节点
        mp_nonfixed_node_t *node = mp_nonfixed_node_addr_skiplist_first(&mp->addr_sl);
        mp_nonfixed_node_addr_skiplist_del(&mp->addr_sl, node);
#if MP_NONFIXED_DBG
        ATOM_FETCH_SUB(&mp->attr->piece_cnt, 1, MORDER_RELAXED);
        ATOM_FETCH_SUB(&mp->attr->free_size, node->size, MORDER_RELAXED);
#endif

        // 持续合并相邻节点
        mp_nonfixed_node_t *next_node = NULL;
        while((next_node = mp_nonfixed_node_addr_skiplist_first(&mp->addr_sl)) && (next_node == (mp_nonfixed_node_t*)(node->data + node->size)))
        {
            mp_nonfixed_node_addr_skiplist_del(&mp->addr_sl, next_node);
#if MP_NONFIXED_DBG
            ATOM_FETCH_SUB(&mp->attr->piece_cnt, 1, MORDER_RELAXED);
            ATOM_FETCH_SUB(&mp->attr->free_size, next_node->size, MORDER_RELAXED);
#endif
            node->size += (sizeof(mp_nonfixed_node_t) + next_node->size);       // size增大，覆盖
        }

        node->used = 0;
        // 合并完，加到新的sl
        mp_nonfixed_node_addr_skiplist_add(&addr_sl_new, node);
        mp_nonfixed_node_skiplist_add(&mp->free_sl, node);
#if MP_NONFIXED_DBG
        ATOM_FETCH_ADD(&mp->attr->piece_cnt, 1, MORDER_RELAXED);
        ATOM_FETCH_ADD(&mp->attr->free_size, node->size, MORDER_RELAXED);
#endif
    }

    int after = mp_nonfixed_node_skiplist_count(&mp->free_sl);

    memcpy(&mp->addr_sl, &addr_sl_new, sizeof(mp_nonfixed_node_addr_skiplist_head_t));

    // 日志记录
    char ctx[SYSLOG_ENTRY_CTX_SIZE] = {};
    snprintf(ctx, sizeof(ctx), "nonfixed memory pool [%s] trigger full merged, piece count %d -> %d", mp->attr->name, before, after);
    syslog_notice(SYSLOG_MODULE_MP, ctx);
}

/**
 * @brief           get node slow path
 * 
 * @param[in]       attr
 * @param[in]       size
 * 
 * @retval          mem
 */
static void* _mp_nofixed_node_get_slow(mem_type_attr_nonfixed_t *attr, uint32_t size)
{
    // 获取nonfixed_mp
    nonfixed_mp_t *mp = _nonfixed_mp_find_or_create(attr, 1);

    // 以当前local队列的长度作为回收阈值
    int count = mp_nonfixed_recycle_spsc_atom_queue_count(&mp->recycle.local);
    for(int i = 0; i < count; ++ i)
    {
        mp_nonfixed_node_t *node = mp_nonfixed_recycle_spsc_atom_queue_pop(&mp->recycle.local);
        if(unlikely(!node))
            break;

        // 尝试合并
        mp_nonfixed_node_t *new_node = _mp_nonfixed_node_merge(mp, node);
        new_node->used = 0;
        mp_nonfixed_node_skiplist_add(&mp->free_sl, new_node);
        mp_nonfixed_node_addr_skiplist_add(&mp->addr_sl, new_node);
#if MP_NONFIXED_DBG
        ATOM_FETCH_ADD(&node->attr->piece_cnt, 1, MORDER_RELAXED);
        ATOM_FETCH_ADD(&node->attr->free_size, new_node->size, MORDER_RELAXED);
#endif
    }

    mp_nonfixed_node_t key = {.size = size};
    mp_nonfixed_node_t *node = mp_nonfixed_node_skiplist_ceil(&mp->free_sl, &key);

    if(likely(node))
    {
        mp_nonfixed_node_skiplist_del(&g_mp_cache->free_sl, node);
        mp_nonfixed_node_addr_skiplist_del(&g_mp_cache->addr_sl, node);
#if MP_NONFIXED_DBG
        ATOM_FETCH_SUB(&attr->piece_cnt, 1, MORDER_RELAXED);
        ATOM_FETCH_SUB(&attr->free_size, node->size, MORDER_RELAXED);
#endif
        _mp_nonfixed_node_split(g_mp_cache, node, size);
        node->used = 1;
        return mp_nonfixed_node_data(node);
    }

    // 全量扫描合并，再尝试一次
    _mp_nonfixed_free_sl_full_merged(mp);
    node = mp_nonfixed_node_skiplist_ceil(&mp->free_sl, &key);
    if(likely(node))
    {
        mp_nonfixed_node_skiplist_del(&g_mp_cache->free_sl, node);
        mp_nonfixed_node_addr_skiplist_del(&g_mp_cache->addr_sl, node);
#if MP_NONFIXED_DBG
        ATOM_FETCH_SUB(&attr->piece_cnt, 1, MORDER_RELAXED);
        ATOM_FETCH_SUB(&attr->free_size, node->size, MORDER_RELAXED);
#endif
        _mp_nonfixed_node_split(g_mp_cache, node, size);
        node->used = 1;
        return mp_nonfixed_node_data(node);
    }

    return NULL;
}

void* _mp_nonfixed_node_get(mem_type_attr_nonfixed_t *attr, uint32_t size)
{
    void *ret = _mp_nonfixed_node_get_fast(attr, size);
    if(likely(ret))
        return ret;

    ret = _mp_nofixed_node_get_slow(attr, size);
    return ret;
}

void _mp_nonfixed_node_put(void *ptr)
{
    if(unlikely(!ptr))
        return;

    // 根据ptr反推node
    mp_nonfixed_node_t *node = mp_nonfixed_data_node(ptr);
    // 获取对应mp
    nonfixed_mp_t *mp = node->attr == g_attr_cache ? g_mp_cache : _nonfixed_mp_find_or_create(node->attr, 0);

    if(mp->recycle.tid == node->tid)
    {
        node->used = 0;
        mp_nonfixed_node_skiplist_add(&mp->free_sl, node);      // 属于本线程的，直接挂回去
        mp_nonfixed_node_addr_skiplist_add(&mp->addr_sl, node);
#if MP_NONFIXED_DBG
        ATOM_FETCH_ADD(&mp->attr->piece_cnt, 1, MORDER_RELAXED);
        ATOM_FETCH_ADD(&mp->attr->free_size, node->size, MORDER_RELAXED);
#endif
    }
    else    // 挂到remote
        mp_nonfixed_recycle_spsc_atom_queue_push(&mp->recycle.remote, node);
}

static void _mp_nonfixed_recycle_thd_wf(void *args)
{
    // 遍历recycle哈希表，把所有remote aq上的节点移动到对应的local aq
    ev_with_rdlock(&g_mp_nonfixed_recycle_hashtable_rwlock)
    {
        mp_nonfixed_recycle_t *re = mp_nonfixed_recycle_hash_first(&g_mp_nonfixed_recycle_hashtable);
        while(re)
        {
            int count = mp_nonfixed_recycle_spsc_atom_queue_count(&re->remote);
            for(int i = 0; i < count; ++ i)
            {
                mp_nonfixed_node_t *node = mp_nonfixed_recycle_spsc_atom_queue_pop(&re->remote);
                if(unlikely(!node))
                    break;
                mp_nonfixed_recycle_t key = {.tid = node->tid};
                mp_nonfixed_recycle_t *target = mp_nonfixed_recycle_hash_find(&g_mp_nonfixed_recycle_hashtable, &key);
                mp_nonfixed_recycle_spsc_atom_queue_push(&target->local, node);
            }
            re = mp_nonfixed_recycle_hash_next(&g_mp_nonfixed_recycle_hashtable, re);
        }
    }
}

void _mp_nonfixed_register(mem_type_attr_nonfixed_t *attr)
{
    // mem type attr加入哈希表
    ev_with_wrlock(&g_mem_type_attr_nonfixed_hashtable_rwlock)
        mem_type_attr_nonfixed_hash_add(&g_mem_type_attr_nonfixed_hashtable, attr);
}

#if MP_NONFIXED_DBG

/**
 * @brief       cli hook for: debug show mp nonfixed
 */
static void* _mp_nonfixed_debug_cli_hook(unsigned char argc, char *argv[])
{
    // 查看attr信息
    ev_with_rdlock(&g_mem_type_attr_nonfixed_hashtable_rwlock)
    {
        safe_printf("\nNONFIXED MP ATTR"
            "\n---------------------------------------------------------------------------\n"
            MP_NONFIXED_ATTR_FMT_HEAD MP_NONFIXED_ATTR_FMT_HEAD, 
            "name", "total_size", "node_cnt", "free_size", "left(%)",
            "----", "----------", "--------", "---------", "-------"
        );
        mem_type_attr_nonfixed_t *attr = mem_type_attr_nonfixed_hash_first(&g_mem_type_attr_nonfixed_hashtable);
        while(attr)
        {
            uint32_t node_cnt = ATOM_LOAD(&attr->piece_cnt, MORDER_RELAXED);
            uint32_t free_size = ATOM_LOAD(&attr->free_size, MORDER_RELAXED);

            safe_printf(MP_NONFIXED_ATTR_FMT_DATA, attr->name, attr->size, node_cnt, free_size, (double)free_size / attr->size * 100.0);
            attr = mem_type_attr_nonfixed_hash_next(&g_mem_type_attr_nonfixed_hashtable, attr);
        }
        safe_printf("---------------------------------------------------------------------------\n");
    }

    // 根据aq查看信息
    ev_with_rdlock(&g_mp_nonfixed_recycle_hashtable_rwlock)
    {
        safe_printf("\nNONFIXED MP AQ"
            "\n---------------------------------------------------------\n"
            MP_NONFIXED_RECYCLE_AQ_FMT_HEAD MP_NONFIXED_RECYCLE_AQ_FMT_HEAD, 
            "name", "tid", "local", "remote", "max_node_size",
            "----", "---", "-----", "------", "-------------"
        );
        mp_nonfixed_recycle_t *re = mp_nonfixed_recycle_hash_first(&g_mp_nonfixed_recycle_hashtable);
        while(re)
        {
            nonfixed_mp_t *mp = container_of(re, nonfixed_mp_t, recycle);
            safe_printf(MP_NONFIXED_RECYCLE_AQ_FMT_DATA, mp->attr->name, re->tid,
                mp_nonfixed_recycle_spsc_atom_queue_count(&re->local),
                mp_nonfixed_recycle_spsc_atom_queue_count(&re->remote),
                mp_nonfixed_node_skiplist_count(&mp->free_sl) ? mp_nonfixed_node_skiplist_last(&mp->free_sl)->size : 0
            );
            re = mp_nonfixed_recycle_hash_next(&g_mp_nonfixed_recycle_hashtable, re);
        }
        safe_printf("---------------------------------------------------------\n");
    }

    return NULL;
}

#endif

void mp_nonfixed_module_init_th()
{
    mem_type_attr_nonfixed_hash_init(&g_mem_type_attr_nonfixed_hashtable);
    mp_nonfixed_recycle_hash_init(&g_mp_nonfixed_recycle_hashtable);

    dbg_major("mp nonfixed module init top half ok");
}

void mp_nonfixed_module_init_bh()
{
    // 启动后台回收
    ev_thd_register(_mp_nonfixed_recycle_thd);
    ev_thd_run(_mp_nonfixed_recycle_thd);

    // 注册调试cli
#if MP_NONFIXED_DBG
    cli_register("debug show mp nonfixed", "dump nonfixed mp debug info", NULL, _mp_nonfixed_debug_cli_hook);
#endif

    dbg_major("mp nonfixed module init bottom half ok");
}