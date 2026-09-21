/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    mp_fixed.c
 * @brief   固定大小内存池
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-08-26
 * @version 1.0
 *
 * @note    之前mp.c实现的有隐蔽bug，这里重构一下，规范命名等
 *
 * @history
 *   1.0 | 2026-08-26 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <string.h>

#include "mp/mp_fixed.h"
#include "event/ev_lock.h"
#include "plat/debug.h"
#include "type/type_list.h"
#include "event/ev_thread.h"
#if MP_FIXED_DBG
#include "cli/cli.h"
#endif

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 预定义list用于存储所有空闲节点
pre_declare_list(mp_fixed_node)
// 预定义hash用于存储所有内存池
pre_declare_hash(fixed_mp)
// 预定义spsc用于回收内存
pre_declare_spsc_atom_queue(mp_fixed_recycle)
// 预定义hash用于管理全部线程回收队列
pre_declare_hash(mp_fixed_recycle)

// 逻辑tid，标识内存节点分配来源线程
typedef uint16_t mp_fixed_tid_t;

// 固定大小内存节点定义
typedef struct{
    mem_type_attr_fixed_t *attr;        // 节点隶属内存属性
    uint16_t size;                      // 用户内存大小
    mp_fixed_tid_t tid;                 // 分配来源线程
    union{
        mp_fixed_node_list_item_t list_item;    // list item
        mp_fixed_recycle_spsc_atom_queue_item_t aq_item;    // spsc aq item
    }item;
    char attr_aligned(8) data[];        // 柔性数组，真正给用户使用的
} attr_aligned(8) mp_fixed_node_t;

// 回收队列相关结构定义
typedef struct{
    mp_fixed_tid_t tid;     // tid
    mp_fixed_recycle_spsc_atom_queue_head_t local;      // 来自本线程的节点
    mp_fixed_recycle_spsc_atom_queue_head_t remote;     // 来自其它线程的节点

    mp_fixed_recycle_hash_item_t item;  // hash item
}mp_fixed_recycle_t;

// 内存池定义
typedef struct{
    mem_type_attr_fixed_t *attr;            // 内存类型
    mp_fixed_node_list_head_t free_list;    // 空闲链表
    mp_fixed_recycle_t recycle;             // 回收相关
    fixed_mp_hash_item_t item;              // hash item
}fixed_mp_t;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// fixed mp动态扩展标记
#define MEM_TYPE_ATTR_FIXED_FLAG_DYNAMIC    (1 << 0)

// 获取node中的data字段
#define mp_fixed_node_data(node)    ((mp_fixed_node_t*)node)->data
// 根据data指针反推node
#define mp_fixed_data_node(_data)   container_of(_data, mp_fixed_node_t, data)

// 获取新的tid
#define mp_fixed_tid_new()          ATOM_FETCH_ADD(&g_tid, 1, MORDER_ACQ_REL)

// 后台回收线程工作间隔
#define MP_FIXED_RECYCLE_INTERVAL   (1000)

#if MP_FIXED_DBG

// attr打印格式，name, size, cnt, used, free
#define MP_FIXED_ATTR_FMT_HEAD      "%-20s%-8s%-8s%-8s%-8s\n"
// attr打印数据格式
#define MP_FIXED_ATTR_FMT_DATA      "%-20s%-8d%-8d%-8d%-8d\n"

// aq打印格式，name, tid, local, remote
#define MP_FIXED_RECYCLE_AQ_FMT_HEAD    "%-20s%-8s%-8s%-8s\n"
// aq打印数据格式
#define MP_FIXED_RECYCLE_AQ_FMT_DATA    "%-20s%-8d%-8d%-8d\n"

#endif

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       cmp func for hash
 */
static attr_pure_inline int _mem_type_attr_fixed_cmp(const mem_type_attr_fixed_t *attr1, const mem_type_attr_fixed_t *attr2)
{
    return strcmp(attr1->name, attr2->name);
}

/**
 * @brief       hash func for hash
 */
static attr_pure_inline unsigned int _mem_type_attr_fixed_hash(const mem_type_attr_fixed_t *attr)
{
    return type_hash_jhash(attr->name, strlen(attr->name), 0);
}

// 定义mem type attr hash操作
declare_hash(mem_type_attr_fixed, mem_type_attr_fixed, mem_type_attr_fixed_t, item, 1, 31, _mem_type_attr_fixed_cmp, _mem_type_attr_fixed_hash)

// 定义空闲节点链表操作
declare_list(mp_fixed_node, mp_fixed_node, mp_fixed_node_t, item.list_item)

/**
 * @brief       cmp func for hash
 */
static attr_pure_inline int _fixed_mp_cmp(const fixed_mp_t *mp1, const fixed_mp_t *mp2)
{
    return strcmp(mp1->attr->name, mp2->attr->name);
}

/**
 * @brief       hash func for hash
 */
static attr_pure_inline unsigned int _fixed_mp_hash(const fixed_mp_t *mp)
{
    return type_hash_jhash(mp->attr->name, strlen(mp->attr->name), 0);
}

// 定义内存池哈希表操作
declare_hash(fixed_mp, fixed_mp, fixed_mp_t, item, 1, 31, _fixed_mp_cmp, _fixed_mp_hash)

/**
 * @brief       cmp func for hash
 */
static attr_pure_inline int _mp_fixed_recycle_cmp(const mp_fixed_recycle_t *r1, const mp_fixed_recycle_t *r2)
{
    return r1->tid - r2->tid;
}

/**
 * @brief       hash func for hash
 */
static attr_pure_inline unsigned int _mp_fixed_recycle_hash(mp_fixed_recycle_t *r)
{
    return type_hash_jhash(&r->tid, sizeof(mp_fixed_tid_t), 0);
}

// 定义回收队列哈希表操作
declare_hash(mp_fixed_recycle, mp_fixed_recycle, mp_fixed_recycle_t, item, 1, 31, _mp_fixed_recycle_cmp, _mp_fixed_recycle_hash)

// 定义回收队列aq操作
declare_spsc_atom_queue(mp_fixed_recycle, mp_fixed_recycle, mp_fixed_node_t, item.aq_item)

/**
 * @brief       supply node from sys for mp
 * 
 * @param[in]   mp  - mp
 * 
 * @note        申请内存补充到池子里
 */
static void _fixed_mp_supply_node(fixed_mp_t *mp);

/**
 * @brief       find or create mp, may supply node
 * 
 * @param[in]   attr
 * @param[in]   supply  0 - not supply node, 1 - supp
 * 
 * @retval      ptr to mp
 * 
 * @note        需要get的线程就需要supply
 */
static fixed_mp_t* _fixed_mp_find_or_create(mem_type_attr_fixed_t *attr, uint8_t supply);

/**
 * @brief       get node fast path
 * 
 * @param[in]   attr
 * 
 * @retval      mem
 */
static attr_hot void* _mp_fixed_node_get_fast(const mem_type_attr_fixed_t *attr);

/**
 * @brief       get node slow path
 * 
 * @param[in]   attr
 * 
 * @retval      mem
 */
static void* _mp_fixed_node_get_slow(mem_type_attr_fixed_t *attr);

/**
 * @brief       backend recycle
 * 
 * @note        后台回收线程工作函数，遍历所有remote aq节点，放置到对应的local aq中
 */
static void _mp_fixed_recycle_thd_work(void *args);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 全局fixed mem type attr hashtable
static mem_type_attr_fixed_hash_head_t g_mem_type_attr_fixed_hashtable = {};
// 锁
static ev_rwlock_t g_mem_type_attr_fixed_hash_rwlock = EV_RWLOCK_INITIALIZER;

// 线程独立mp hashtable，无锁
static thread_local fixed_mp_hash_head_t g_fixed_mp_hashtable = {};

// 线程当前attr cache
static thread_local mem_type_attr_fixed_t *g_mem_type_attr_fixed_cache = NULL;
// 线程当前mp cache
static thread_local fixed_mp_t *g_fixed_mp_cache = NULL;

// 全局tid
static ATOMIC_UINT16_T g_tid = 0;

// 全局hash，管理所有内存池的recycle aq
static mp_fixed_recycle_hash_head_t g_fixed_mp_recycle_aq_hashtable = {};
// hash锁
static ev_rwlock_t g_fixed_mp_recycle_aq_hash_rwlock = EV_RWLOCK_INITIALIZER;

// 后台回收线程
declare_ev_thd(mp_fixed_recycle_thd, _mp_fixed_recycle_thd_work, NULL, MP_FIXED_RECYCLE_INTERVAL)

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void _mem_type_attr_fixed_register(mem_type_attr_fixed_t *attr)
{
    // 加到全局attr fixed哈希表中
    ev_with_wrlock(&g_mem_type_attr_fixed_hash_rwlock)
        mem_type_attr_fixed_hash_add(&g_mem_type_attr_fixed_hashtable, attr);
}

static void _fixed_mp_supply_node(fixed_mp_t *mp)
{
    mem_type_attr_fixed_t *attr = mp->attr;
    uint32_t count = mp_fixed_node_list_count(&mp->free_list);
    while(count++ < attr->node_cnt)
    {
        mp_fixed_node_t *node = mp_calloc(1, sizeof(mp_fixed_node_t) + attr->node_size);
        node->attr = attr;
        node->size = attr->node_size;
        node->tid = mp->recycle.tid;
        mp_fixed_node_list_add_head(&mp->free_list, node);
#if MP_FIXED_DBG
        ATOM_FETCH_ADD(&attr->free, 1, MORDER_ACQ_REL);
#endif
    }
}

static fixed_mp_t* _fixed_mp_find_or_create(mem_type_attr_fixed_t *attr, uint8_t supply)
{
    // 检查是否初始化线程专用hashtable
    fixed_mp_hash_head_t empty = {};
    if(unlikely(!memcmp(&empty, &g_fixed_mp_hashtable, sizeof(fixed_mp_hash_head_t))))
        fixed_mp_hash_init(&g_fixed_mp_hashtable);

    // 从线程独立mp hash中搜索，无需锁
    fixed_mp_t key = {.attr = attr};
    fixed_mp_t *mp = fixed_mp_hash_find(&g_fixed_mp_hashtable, &key);
    if(likely(mp))
        goto done;

    // 搜索不到，需要初始化，加入全局哈希
    mp_fixed_tid_t tid = mp_fixed_tid_new();        // 获取新的tid
    mp = (fixed_mp_t*)mp_calloc(1, sizeof(fixed_mp_t));
    mp->attr = attr;
    mp_fixed_node_list_init(&mp->free_list);
    mp_fixed_recycle_spsc_atom_queue_init(&mp->recycle.local);
    mp_fixed_recycle_spsc_atom_queue_init(&mp->recycle.remote);
    mp->recycle.tid = tid;
    ev_with_wrlock(&g_fixed_mp_recycle_aq_hash_rwlock)      // 加到全局hash中
        mp_fixed_recycle_hash_add(&g_fixed_mp_recycle_aq_hashtable, &mp->recycle);
    if(supply)
        _fixed_mp_supply_node(mp);
    fixed_mp_hash_add(&g_fixed_mp_hashtable, mp);

done:
    g_mem_type_attr_fixed_cache = attr;
    g_fixed_mp_cache = mp;
    return mp;
}

static void* _mp_fixed_node_get_fast(const mem_type_attr_fixed_t *attr)
{
    // 检查当前cache
    if(unlikely(attr != g_mem_type_attr_fixed_cache))
        return NULL;
    // cache匹配，直接pop一个空闲节点
    const mp_fixed_node_t *node = mp_fixed_node_list_pop(&g_fixed_mp_cache->free_list);
    if(unlikely(!node))
        return NULL;
    return mp_fixed_node_data(node);
}

static void* _mp_fixed_node_get_slow(mem_type_attr_fixed_t *attr)
{
    // 根据attr获取对应mp
    fixed_mp_t *mp = _fixed_mp_find_or_create(attr, 1);

    // 从local aq回收节点
    unsigned int count = mp_fixed_recycle_spsc_atom_queue_count(&mp->recycle.local);
    for(int i = 0; i < count; ++ i)
    {
        mp_fixed_node_t *node = mp_fixed_recycle_spsc_atom_queue_pop(&mp->recycle.local);
        if(unlikely(!node))
            break;
        mp_fixed_node_list_add_head(&mp->free_list, node);
#if MP_FIXED_DBG
        ATOM_FETCH_ADD(&attr->free, 1, MORDER_ACQ_REL);
        ATOM_FETCH_SUB(&attr->used, 1, MORDER_ACQ_REL);
#endif
    }

    // 从mp pop一个节点
    mp_fixed_node_t *node = mp_fixed_node_list_pop(&mp->free_list);
    if(likely(node))
    {
#if MP_FIXED_DBG
        ATOM_FETCH_ADD(&attr->used, 1, MORDER_ACQ_REL);
        ATOM_FETCH_SUB(&attr->free, 1, MORDER_ACQ_REL);
#endif
        return mp_fixed_node_data(node);
    }

    return NULL;
}

void* _mp_fixed_node_get(mem_type_attr_fixed_t *attr)
{
    void *ret = _mp_fixed_node_get_fast(attr);
    if(likely(ret))
    {
#if MP_FIXED_DBG
        ATOM_FETCH_ADD(&attr->used, 1, MORDER_ACQ_REL);
        ATOM_FETCH_SUB(&attr->free, 1, MORDER_ACQ_REL);
#endif
        return ret;
    }
    return _mp_fixed_node_get_slow(attr);
}

void _mp_fixed_node_put(void *data)
{
    if(unlikely(!data))
        return;

    // 反推node
    mp_fixed_node_t *node = mp_fixed_data_node(data);
    // 获取对应mp
    fixed_mp_t *mp = NULL;
    if(node->attr == g_mem_type_attr_fixed_cache)
        mp = g_fixed_mp_cache;
    else
        mp = _fixed_mp_find_or_create(node->attr, 0);

    // 根据tid回收
    if(node->tid == mp->recycle.tid)
        mp_fixed_node_list_add_head(&mp->free_list, node);      // 本线程的直接挂到空闲链表
    else
        mp_fixed_recycle_spsc_atom_queue_push(&mp->recycle.remote, node);
}

static void _mp_fixed_recycle_thd_work(void *args)
{
    ev_with_rdlock(&g_fixed_mp_recycle_aq_hash_rwlock)
    {
        mp_fixed_recycle_t *recycle = mp_fixed_recycle_hash_first(&g_fixed_mp_recycle_aq_hashtable);
        while(recycle)
        {
            unsigned int count = mp_fixed_recycle_spsc_atom_queue_count(&recycle->remote);
            for(int i = 0; i < count; ++ i)
            {
                mp_fixed_node_t *node = mp_fixed_recycle_spsc_atom_queue_pop(&recycle->remote);
                if(unlikely(!node))
                    break;
                // 查找对应recycle
                mp_fixed_recycle_t key = {.tid = node->tid};
                mp_fixed_recycle_t *target = mp_fixed_recycle_hash_find(&g_fixed_mp_recycle_aq_hashtable, &key);
                mp_fixed_recycle_spsc_atom_queue_push(&target->local, node);
            }
            recycle = mp_fixed_recycle_hash_next(&g_fixed_mp_recycle_aq_hashtable, recycle);
        }
    }
}

#if MP_FIXED_DBG

void mp_fixed_debug_show(unsigned char argc, char *argv[])
{
    // 查看attr信息
    ev_with_rdlock(&g_mem_type_attr_fixed_hash_rwlock)
    {
        safe_printf("\n                  MP_FIXED_ATTR\n");
        safe_printf("------------------------------------------------\n" MP_FIXED_ATTR_FMT_HEAD MP_FIXED_ATTR_FMT_HEAD, 
            "name", "size", "count", "used", "free",
            "----", "----", "-----", "----", "----");
        mem_type_attr_fixed_t *attr = mem_type_attr_fixed_hash_first(&g_mem_type_attr_fixed_hashtable);
        while(attr)
        {
            safe_printf(MP_FIXED_ATTR_FMT_DATA, attr->name, attr->node_size, attr->node_cnt, ATOM_LOAD(&attr->used, MORDER_ACQUIRE), ATOM_LOAD(&attr->free, MORDER_ACQUIRE));
            attr = mem_type_attr_fixed_hash_next(&g_mem_type_attr_fixed_hashtable, attr);
        }
        safe_printf("------------------------------------------------\n");
    }

    // 查看aq信息
    ev_with_rdlock(&g_fixed_mp_recycle_aq_hash_rwlock)
    {
        safe_printf("\n           MP_FIXED_RECYCLE_QUEUE\n");
        safe_printf("------------------------------------------\n" MP_FIXED_RECYCLE_AQ_FMT_HEAD MP_FIXED_RECYCLE_AQ_FMT_HEAD,
            "attr_name", "tid", "local", "remote",
            "---------", "---", "-----", "------"
        );
        mp_fixed_recycle_t *recycle = mp_fixed_recycle_hash_first(&g_fixed_mp_recycle_aq_hashtable);
        while(recycle)
        {
            // 根据recycle反推mp
            fixed_mp_t *mp = container_of(recycle, fixed_mp_t, recycle);
            safe_printf(MP_FIXED_RECYCLE_AQ_FMT_DATA,
                mp->attr->name, recycle->tid,
                mp_fixed_recycle_spsc_atom_queue_count(&recycle->local),
                mp_fixed_recycle_spsc_atom_queue_count(&recycle->remote)
            );
            recycle = mp_fixed_recycle_hash_next(&g_fixed_mp_recycle_aq_hashtable, recycle);
        }
        safe_printf("------------------------------------------\n");
    }
}

#else

void mp_fixed_debug_show(unsigned char argc, char *argv[])
{
    return;
}

#endif

void mp_fixed_module_init_th()
{
    // 全局变量初始化
    mem_type_attr_fixed_hash_init(&g_mem_type_attr_fixed_hashtable);
    mp_fixed_recycle_hash_init(&g_fixed_mp_recycle_aq_hashtable);

    dbg_major("mp fixed module init th ok");
}

void mp_fixed_module_init_bh()
{
    // 启动后台回收
    ev_thd_register(mp_fixed_recycle_thd);
    ev_thd_run(mp_fixed_recycle_thd);
}