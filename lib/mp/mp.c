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
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "mp/mp.h"
#include "plat/atom.h"
#include "event/ev_thread.h"
#include "cli/cli.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define RECYCLE_WAKE_THRESHOLD  (64)   // 唤醒阈值

// 非固定大小内存池大小4MB
#define NONFIXED_MP_MEM_SIZE    (4 << 20)

// 非固定大小内存碎片阈值，64B给用户使用
#define NONFIXED_MP_MEM_PIECE   (64)

// 获取node中的data给用户
#define mp_fixed_node_data(_node)   (_node)->data;
// 根据data获取node
#define mp_fixed_data_node(_data)   container_of(_data, fixed_mem_node_t, data)

// 获取nonfixed node中的data
#define mp_nonfixed_node_data(_node)    (_node)->data
// 根据data获取nonfixed node
#define mp_nonfixed_data_node(_data)    container_of(_data, nonfixed_mem_node_t, data)

// 打印attr的格式 标题：name,type,node_size,total,use,free,usage(%),piece
#define MEM_ATTR_FMT_HEAD       "%-20s%-12s%-12s%-10s%-8s%-8s%-10s%-8s\n"
// mp fixed 打印attr的格式 数据：name,type,total,use,free,usage
#define MEM_ATTR_FIXED_FMT_DATA       "%-20s%-12s%-12d%-10d%-8d%-8d%-10.3f%-8s\n"
// mp non fixed 打印attr的格式 数据：name,type,total,use,free,usage
#define MEM_ATTR_NONFIXED_FMT_DATA    "%-20s%-12s%-12s%-10d%-8d%-8d%-10.3f%-8d\n"

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
 * @brief       free list cmp func
 * 
 * @param[in]   l1  - list 1
 * @param[in]   l2  - list 2
 * 
 * @retval      cmp val
 * 
 * @note        cmp by attr name
 */
static attr_pure_inline int _fixed_free_list_cmp(fixed_free_list_t *l1, fixed_free_list_t *l2)
{
    assert(l1 && l1->attr && l1->attr->name && l2 && l2->attr && l2->attr->name);
    return strcmp(l1->attr->name, l2->attr->name);
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
 * @brief       fixed free list hash func
 * 
 * @param[in]   l   - list
 * 
 * @retval      hash val
 * 
 * @note        hash by attr name
 */
static attr_pure_inline unsigned int _fixed_free_list_hash(fixed_free_list_t *l)
{
    assert(l && l->attr && l->attr->name);
    return type_hash_jhash(l->attr->name, strlen(l->attr->name), 0);
}

/**
 * @brief       check if attr fixed_size
 * 
 * @param[in]   attr    - mem type attr
 * 
 * @retval      0 - no, else - yes
 */
static attr_pure_inline unsigned int mem_type_attr_fixed_size(mem_type_attr_t *attr)
{
    return attr->flag & MEM_TYPE_ATTR_FIXED_SIZE;
}

/**
 * @brief       new a logic tid
 * 
 * @note        分配一个新的逻辑ID，原子ADD保证并发可用
 */
static attr_force_inline tid_t tid_new();

/**
 * @brief       获取tid
 * 
 * @param[in]
 */
static attr_pure_inline tid_t tid_get(mem_type_attr_t *attr);

static attr_pure_inline int _mp_fixed_node_belong_local(fixed_mem_node_t *node)
{
    return node->tid == tid_get(node->attr);
}

/**
 * @brief       put node local
 * 
 * @param[in]   free_list   - ptr to free_list
 * @param[in]   node        - ptr to node
 */
static attr_force_inline void _mp_fixed_node_put_local(fixed_free_list_t *free_list, fixed_mem_node_t *node);

/**
 * @brief       put node remote
 * 
 * @param[in]   aq_head - remote aq head
 * @param[in]   node    - ptr to node
 */
static attr_force_inline void _mp_fixed_node_put_remote(fixed_recycle_spsc_atom_queue_head_t *aq_head, fixed_mem_node_t *node);

/**
 * @brief       find free_list_t ptr by tid
 * 
 * @param[in]   tid     - tid
 */
static attr_force_inline fixed_free_list_t* _fixed_free_list_head_hash_find_by_tid(tid_t tid);

/**
 * @brief       new fixed mem node from system
 * 
 * @param[in]   attr    - mem type attr
 * 
 * @retval      ptr to node
 */
static attr_force_inline fixed_mem_node_t* fixed_mem_node_new(mem_type_attr_t *attr)
{
    assert(mem_type_attr_fixed_size(attr));     // 检查是否固定大小

    fixed_mem_node_t* node = mp_calloc(1, sizeof(fixed_mem_node_t)+attr->node_size);    // 柔性数组分配需添加node_size
    assert(node);

    node->size = attr->node_size;
    node->attr = attr;
    node->tid = tid_get(attr);

    // 更新分配计数
    ATOM_FETCH_ADD(&attr->allocated, 1, MORDER_ACQ_REL);

    return node;
}

/**
 * @brief       init gloabl variable: g_mem_type_attr_hash_head
 * 
 * @note        CTOR
 */
static attr_force_inline void g_mem_type_attr_hash_init() attr_ctor(CTOR_PRIO_MID);

/**
 * @brief       init gloabl variable: g_fixed_free_list_hash_head
 * 
 * @note        CTOR
 */
static attr_force_inline void g_fixed_free_list_head_hash_init() attr_ctor(CTOR_PRIO_MID);

/**
 * @brief       init global variable: g_fixed_recycle_aq_list_head
 * 
 * @note        CTOR
 */
static attr_force_inline void g_fixed_recycle_aq_list_head_init() attr_ctor(CTOR_PRIO_MID);

/**
 * @brief       recycle fixed node work func
 * 
 * @note        固定大小节点内存回收线程的工作函数
 */
static attr_force_inline void fixed_node_recycle_work(void *args);

/**
 * @brief       init recycle thread
 * 
 * @note        CTOR，启动回收线程
 */
static attr_force_inline void fixed_node_recycle_init() attr_ctor(CTOR_PRIO_LOW);

/**
 * @brief       ctor register mp cli
 */
static attr_force_inline void mp_cli_init() attr_ctor(CTOR_PRIO_MID);

/**
 * @brief       cli hook for: show mp
 */
static void* _mp_show_mp_hook(unsigned char argc, char *argv[]);

/**
 * @brief       find mp fixed free list of self thread, if not exist, create it.
 * 
 * @param[in]   attr    - mem type attr
 * @param[in]   supply  - if create, supply or not
 * 
 * @retval      ptr to free list
 */
static attr_force_inline fixed_free_list_t* _mp_fixed_free_list_get_or_create(mem_type_attr_t *attr, int supply);

/**
 * @brief       auto init global free list hash table
 * 
 * @note        检查线程所见的hashtable是否初始化，没有的话，调用
 */
static void _g_fixed_free_list_hash_init();

/**
 * @brief       cmp two nonfixed mp
 * 
 * @param[in]   p1  - mp 1
 * @param[in]   p2  - mp 2
 * 
 * @retval      cmp val
 * 
 * @note        cmp by attr->name
 */
static attr_pure_inline int _nonfixed_mp_cmp(nonfixed_mp_t *p1, nonfixed_mp_t *p2)
{
    assert(p1 && p1->attr && p1->attr->name && p2 && p2->attr && p2->attr->name);
    return strcmp(p1->attr->name, p2->attr->name);
}

/**
 * @brief       hash nonfixed mp
 * 
 * @param[in]   p   - mp
 * 
 * @retval      hash val
 * 
 * @note        hash by attr->name
 */
static attr_pure_inline int _nonfixed_mp_hash(nonfixed_mp_t *p)
{
    assert(p && p->attr && p->attr->name);
    return type_hash_jhash(p->attr->name, strlen(p->attr->name), 0);
}

/**
 * @brief       cmp two non fixed mem node
 * 
 * @param[in]   n1  - node 1
 * @param[in]   n2  - node 2
 * 
 * @retval      cmp val
 * 
 * @note        cmp by size
 */
static attr_pure_inline int _nonfixed_mem_node_cmp(nonfixed_mem_node_t *n1, nonfixed_mem_node_t *n2)
{
    assert(n1 && n2);
    return n1->size - n2->size;
}

/**
 * @brief       check if nonfixed mem node in mem pool
 * 
 * @param[in]   mp      - mp
 * @param[in]   node    - mem node
 * 
 * @retval      1 - valid, 0 - invalid
 */
static attr_pure_inline int _nonfixed_mem_node_valid(nonfixed_mp_t *mp, nonfixed_mem_node_t *node)
{
    char *address = (char*)node;
    char *start = (char*)mp->pool_start, *end = (char*)mp->pool_end;
    return (address >= start) && (address < end);
}

/**
 * @brief       cmp two nonfixed recycle aq
 * 
 * @param[in]   a1      - aq 1
 * @param[in]   a2      - aq 2
 * 
 * @retval      cmp val
 * 
 * @note        cmp by aq tid
 */
static attr_pure_inline int _nonfixed_recycle_aq_cmp(nonfixed_recycle_aq_t *a1, nonfixed_recycle_aq_t *a2)
{
    assert(a1 && a2);
    return a1->tid - a2->tid;
}

/**
 * @brief       hash nonfixed recycle aq
 * 
 * @param[in]   a   - aq
 * 
 * @retval      hash val
 * 
 * @note        hash by tid
 */
static attr_pure_inline unsigned int _nonfixed_recycle_aq_hash(nonfixed_recycle_aq_t *a)
{
    assert(a);
    return type_hash_jhash(&a->tid, sizeof(tid_t), 0);
}

// 定义非固定大小内存池哈希表操作
declare_hash(nonfixed_mp, nonfixed_mp, nonfixed_mp_t, item, 1, 31, _nonfixed_mp_cmp, _nonfixed_mp_hash)

// 定义非固定大小空闲跳表操作
declare_skiplist(nonfixed_free, nonfixed_free, nonfixed_mem_node_t, item.sl_item, _nonfixed_mem_node_cmp)

// 定义回收队列哈希操作
declare_hash(nonfixed_recycle_aq, nonfixed_recycle_aq, nonfixed_recycle_aq_t, item, 1, 31, _nonfixed_recycle_aq_cmp, _nonfixed_recycle_aq_hash)

// 定义回收spsc队列操作
declare_spsc_atom_queue(nonfixed_recycle, nonfixed_recycle, nonfixed_mem_node_t, item.aq_item)

/**
 * @brief       find thread local nonfixed mp by attr, if not exist, create it.
 * 
 * @param[in]   attr    - mem type attr
 * @param[in]   supply  - if create, supply node or not
 * 
 * @retval      ptr to mp
 */
static nonfixed_mp_t* _nonfixed_mp_find_or_create(mem_type_attr_t *attr, int supply);

/**
 * @brief       work func of ev thd: fixed_node_recycle
 * 
 * @note        后台回收非固定大小内存节点的工作函数
 */
static void _nonfixed_node_recycle_work(void *args);

/**
 * @brief       ctor init non fixed mp
 * 
 * @note        构造初始化nonfixed mp相关
 */
static void nonfixed_mp_early_init() attr_ctor(CTOR_PRIO_HIGH);

/**
 * @brief       ctor run ev thd for nonfixed recycle
 * 
 * @note        declare_ev_thd使用MID，这里必须使用比他低的
 */
static void nonfixed_recycle_thd_init() attr_ctor(CTOR_PRIO_LOW);

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

// 全局哈希表，存储所有内存类型属性
static mem_type_attr_hash_head_t g_mem_type_attr_hash_head = {};

// 全局哈希表，存储所有固定大小空闲链表头。使用线程私有属性，每个线程持有一个同名的副本，互不干扰，做到隔离
static thread_local fixed_free_list_head_hash_head_t g_fixed_free_list_hash_head = {};

// 全局tid，用于自行管理分配逻辑线程ID
static tid_t g_tid = 0;
// 全局链表，存储所有线程独立的回收队列
static fixed_recycle_aq_list_head_t g_fixed_recycle_aq_list_head = {};
// 声明回收事件线程，兜底1000ms回收一次
declare_ev_thd(fixed_node_recycle, fixed_node_recycle_work, NULL, 1000)

// 全局统计数据
ATOMIC_UINT32_T g_mp_calloc_cnt = 0;     // 申请次数
ATOMIC_UINT64_T g_mp_calloc_size = 0;    // 申请大小
ATOMIC_UINT32_T g_mp_free_cnt = 0;       // 释放次数
ATOMIC_UINT64_T g_mp_free_size = 0;      // 释放大小

// 线程独立的缓存，当前attr
static thread_local mem_type_attr_t *g_attr_cache = NULL;
// 线程独立的缓存，当前free_list
static thread_local fixed_free_list_t *g_fixed_free_list = NULL;

// 全局哈希表，存储所有非固定大小内存池
static thread_local nonfixed_mp_hash_head_t g_nonfixed_mp_hash = {};
// 哈希表初始化标志
static thread_local int g_nonfixed_mp_hash_init_flag = 0;

// 全局链表，存储所有非固定大小的回收队列
static nonfixed_recycle_aq_hash_head_t g_nonfixed_recycle_aq_hash = {};

// 缓存非固定内存池指针
static thread_local nonfixed_mp_t *g_nonfixed_mp_cache = NULL;

// 后台线程，用来跨线程回收非固定大小节点，1s自动工作一次
declare_ev_thd(nonfixed_node_recycle, _nonfixed_node_recycle_work, NULL, 1000)

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

// 定义内存类型属性哈希表相关操作
declare_hash(mem_type_attr, mem_type_attr, mem_type_attr_t, item, 1, 31, _mem_type_attr_cmp, _mem_type_attr_hash)

// 定义空闲链表头哈希表相关操作
declare_hash(fixed_free_list_head, fixed_free_list_head, fixed_free_list_t, item, 1, 31, _fixed_free_list_cmp, _fixed_free_list_hash)

// 定义回收队列链表相关操作
declare_list(fixed_recycle_aq, fixed_recycle_aq, fixed_recycle_aq_t, item)

// 定义回收队列的相关操作
declare_spsc_atom_queue(fixed_recycle, fixed_recycle, fixed_mem_node_t, aq_item)

static inline void g_mem_type_attr_hash_init()
{
    mem_type_attr_hash_init(&g_mem_type_attr_hash_head);
}

static inline void g_fixed_free_list_head_hash_init()
{
    fixed_free_list_head_hash_init(&g_fixed_free_list_hash_head);
}

static inline void g_fixed_recycle_aq_list_head_init()
{
    fixed_recycle_aq_list_init(&g_fixed_recycle_aq_list_head);
}

void mem_type_attr_init(mem_type_attr_t *attr)
{
    assert(attr && attr->name);

    ATOM_STORE(&attr->allocated, 0, MORDER_RELEASE);
#if MP_DBG_MODE
    ATOM_STORE(&attr->used, 0, MORDER_RELEASE);
#endif

    mem_type_attr_hash_add(&g_mem_type_attr_hash_head, attr);
}

void _mp_fixed_init(mem_type_attr_t *attr)
{
    // 重要：检查g_fixed_free_list_hash_head是否空，是的话先初始化一下
    _g_fixed_free_list_hash_init();

    assert(attr && attr->name && mem_type_attr_fixed_size(attr));   // 检查参数

    // 在线程私有的g_fixed_free_list_hash_head哈希表中查找是否存在对应free list
    fixed_free_list_t key = {.attr = attr};
    fixed_free_list_t *free_list = fixed_free_list_head_hash_find(&g_fixed_free_list_hash_head, &key);
    if(!free_list)  // 没有对应free_list，则创建
    {
        free_list = mp_calloc(1, sizeof(fixed_free_list_t));
        assert(free_list);
        free_list->attr = attr;
        fixed_free_list_init(&free_list->head);     // 初始化空闲链表头
        fixed_free_list_head_hash_add(&g_fixed_free_list_hash_head, free_list);     // 加到全局哈希表

        // 初始化内存回收队列相关
        free_list->recycle_aq_head.tid = tid_new();
        fixed_recycle_spsc_atom_queue_init(&free_list->recycle_aq_head.local_recycle_aq_head);
        fixed_recycle_spsc_atom_queue_init(&free_list->recycle_aq_head.remote_recycle_aq_head);
        // 加到全局链表
        fixed_recycle_aq_list_add_tail(&g_fixed_recycle_aq_list_head, &free_list->recycle_aq_head);
    }
}

void fixed_mp_supply(mem_type_attr_t *attr)
{
    int count = 0;
    fixed_mem_node_t *node = NULL;

    assert(attr);

    fixed_free_list_t key = {.attr = attr};
    fixed_free_list_t *free_list = fixed_free_list_head_hash_find(&g_fixed_free_list_hash_head, &key);
    assert(free_list);

    fixed_free_list_head_t *head = &free_list->head;

    while(fixed_free_list_count(head) < attr->node_max_num)
    {
        node = fixed_mem_node_new(attr);
        fixed_free_list_add_head(head, node);
    }
}

static void _g_fixed_free_list_hash_init()
{
    fixed_free_list_head_hash_head_t empty = {};
    if(!memcmp(&g_fixed_free_list_hash_head, &empty, sizeof(fixed_free_list_head_hash_head_t)))
        fixed_free_list_head_hash_init(&g_fixed_free_list_hash_head);
}

static inline fixed_free_list_t* _mp_fixed_free_list_get_or_create(mem_type_attr_t *attr, int supply)
{
    assert(attr);

    if(attr == g_attr_cache)
        return g_fixed_free_list;

    g_attr_cache = attr;

    // 初始化哈希表，若有必要
    _g_fixed_free_list_hash_init();
    // 先查找哈希表中是否存在对应记录
    fixed_free_list_t key = {.attr = attr};
    fixed_free_list_t *free_list = fixed_free_list_head_hash_find(&g_fixed_free_list_hash_head, &key);
    
    if(free_list)
    {
        g_fixed_free_list = free_list;
        return free_list;
    }

    // 如果不存在，说明没有显式初始化，这里初始化，并且补充节点
    _mp_fixed_init(attr);
    if(supply)
        fixed_mp_supply(attr);

    free_list = fixed_free_list_head_hash_find(&g_fixed_free_list_hash_head, &key);
    assert(free_list);

    g_fixed_free_list = free_list;

    return free_list;
}

void* _mp_fixed_node_get(mem_type_attr_t *attr)
{
    if(!mem_type_attr_fixed_size(attr))     // 检查是否固定大小
        return NULL;

    // 找到free list，没有的话创建
    fixed_free_list_t *free_list = _mp_fixed_free_list_get_or_create(attr, 1);

    // 将local aq中的节点都归还回来
    fixed_mem_node_t *recycle_node = NULL;
    while(recycle_node = fixed_recycle_spsc_atom_queue_pop(&free_list->recycle_aq_head.local_recycle_aq_head))
    {
        fixed_free_list_add_head(&free_list->head, recycle_node);
    }

    if(!fixed_free_list_count(&free_list->head))    // 检查空闲节点个数
        return NULL;

    fixed_mem_node_t *node = fixed_free_list_pop(&free_list->head); // pop一个节点

#if MP_DBG_MODE
    // 更新使用计数
    ATOM_FETCH_ADD(&attr->used, 1, MORDER_ACQ_REL);
#endif

    return mp_fixed_node_data(node);
}

static inline void _mp_fixed_node_put_local(fixed_free_list_t *free_list, fixed_mem_node_t *node)
{
    fixed_free_list_add_head(&free_list->head, node);   // 返回空闲链表
}

static inline void _mp_fixed_node_put_remote(fixed_recycle_spsc_atom_queue_head_t *aq_head, fixed_mem_node_t *node)
{
    // 将node放置到本线程的remote aq中
    fixed_recycle_spsc_atom_queue_push(aq_head, node);

    // 唤醒回收线程
    if(fixed_recycle_spsc_atom_queue_count(aq_head) >= RECYCLE_WAKE_THRESHOLD)
        ev_thd_wake(fixed_node_recycle);
}

void _mp_fixed_node_put(void *ptr)
{
    assert(ptr);
    fixed_mem_node_t *node = mp_fixed_data_node(ptr);
    assert(node && mem_type_attr_fixed_size(node->attr));       // 确认是否fixed

    // 找到free list，没有的话创建
    fixed_free_list_t *free_list = _mp_fixed_free_list_get_or_create(node->attr, 0);

    // 本地归还 or 异地归还
    if(node->tid == free_list->recycle_aq_head.tid)
        _mp_fixed_node_put_local(free_list, node);
    else
        _mp_fixed_node_put_remote(&free_list->recycle_aq_head.remote_recycle_aq_head, node);

#if MP_DBG_MODE
    // 更新使用计数
    ATOM_FETCH_SUB(&node->attr->used, 1, MORDER_ACQ_REL);
#endif
}

static inline tid_t tid_new()
{
    return ATOM_ADD_FETCH(&g_tid, 1, MORDER_ACQ_REL);
}

static inline tid_t tid_get(mem_type_attr_t *attr)
{
    assert(attr);
    fixed_free_list_t key = {.attr = attr};
    fixed_free_list_t *free_list = fixed_free_list_head_hash_find(&g_fixed_free_list_hash_head, &key);
    assert(free_list);
    return free_list->recycle_aq_head.tid;
}

static inline fixed_free_list_t* _fixed_free_list_head_hash_find_by_tid(tid_t tid)
{
    fixed_free_list_t *fl = fixed_free_list_head_hash_first(&g_fixed_free_list_hash_head);
    while(fl)
    {
        if(fl->recycle_aq_head.tid == tid)
            return fl;
        fl = fixed_free_list_head_hash_first(&g_fixed_free_list_hash_head);
    }
    return NULL;
}

static inline void fixed_node_recycle_work(void *args)
{
    // 遍历所有aq，逐一将节点挂到正确的aq上
    fixed_recycle_aq_t *aq = fixed_recycle_aq_list_first(&g_fixed_recycle_aq_list_head);
    while(aq)
    {
        fixed_mem_node_t *node = NULL;
        while(node = fixed_recycle_spsc_atom_queue_pop(&aq->remote_recycle_aq_head))
        {
            // 找到对应aq_t，挂到其local队列
            fixed_recycle_aq_t *aq_target = fixed_recycle_aq_list_first(&g_fixed_recycle_aq_list_head);
            while(aq_target)
            {
                if(aq_target->tid == node->tid)
                {
                    fixed_recycle_spsc_atom_queue_push(&aq_target->local_recycle_aq_head, node);
                    break;
                }
                aq_target = fixed_recycle_aq_list_next(aq_target);
            }
            assert(aq_target);
        }
        aq = fixed_recycle_aq_list_next(aq);
    }
}

static inline void fixed_node_recycle_init()
{
    ev_thd_run(fixed_node_recycle);
}

static void nonfixed_mp_early_init()
{
    // 初始化存储nonfixed mp的哈希表，这里只有主线程做了。子线程需要检查flag并且初始化
    nonfixed_mp_hash_init(&g_nonfixed_mp_hash);
    g_nonfixed_mp_hash_init_flag = 1;

    // 初始化存储所有recycle aq的哈希表
    nonfixed_recycle_aq_hash_init(&g_nonfixed_recycle_aq_hash);
}

/**
 * @brief       del nonfixed mem node from free skiplist
 * 
 * @param[in]   sl      - free skiplist
 * @param[in]   node    - mem node
 * 
 * @note        将空闲节点加入到跳表中
 */
static attr_force_inline void _mp_nonfixed_men_node_del_fsl(nonfixed_free_skiplist_head_t *sl, nonfixed_mem_node_t *node)
{
    node->fl = 0;                               // 设置位于空闲跳表的标志位
    nonfixed_free_skiplist_del(sl, node);       // 移出跳表
}

/**
 * @brief       add nonfixed mem node into free skiplist
 * 
 * @param[in]   sl      - free skiplist
 * @param[in]   node    - mem node
 * 
 * @note        将空闲节点加入到跳表中，可以的话进行合并
 */
static attr_force_inline void _mp_nonfixed_men_node_add_fsl(nonfixed_mp_t *mp, nonfixed_mem_node_t *node)
{
    nonfixed_free_skiplist_head_t *sl = &mp->free_sl;
    // 尝试前向合并
    if(node->prev_size)
    {
        // 获取前驱的头部
        nonfixed_mem_node_t *prev = (nonfixed_mem_node_t*)((char*)node - node->prev_size - sizeof(nonfixed_mem_node_t));
        if(_nonfixed_mem_node_valid(mp, prev) && prev->fl)        // 前驱也在空闲跳表中，把前驱移出来，合并，再加回去
        {
            _mp_nonfixed_men_node_del_fsl(sl, prev);        // 移出跳表
            prev->size = prev->size + node->size + sizeof(nonfixed_mem_node_t); // 吞并node
            node = prev;        // node指向前驱，也就是新的大节点
            // 更新后继的prev_size
            nonfixed_mem_node_t *next = (nonfixed_mem_node_t*)((char*)node + sizeof(nonfixed_mem_node_t) + node->size);
            if(_nonfixed_mem_node_valid(mp, next))
                next->prev_size = node->size;
#if MP_DBG_MODE
            ATOM_SUB_FETCH(&mp->attr->piece, 1, MORDER_RELAXED);    // 碎片减少
            // 多出一个Head的内存可以给用户使用
            ATOM_SUB_FETCH(&mp->attr->used, sizeof(nonfixed_mem_node_t), MORDER_RELAXED);
#endif
        }
    }

    // Node加入跳表
    node->fl = 1;                               // 设置位于空闲跳表的标志位
    nonfixed_free_skiplist_add(sl, node);       // 加入跳表
}

void _mp_nonfixed_init(mem_type_attr_t *attr)
{
    // 检查内存类型为非固定大小
    assert(attr && attr->name && 0 == mem_type_attr_fixed_size(attr));

    // 在全局哈希表中查找是否已存在mp
    nonfixed_mp_t key = {.attr = attr};
    nonfixed_mp_t *mp = nonfixed_mp_hash_find(&g_nonfixed_mp_hash, &key);
    if(!mp)     // 查找失败则创建
    {
        mp = (nonfixed_mp_t*)mp_calloc(1, sizeof(nonfixed_mp_t));
        // 组件初始化
        mp->attr = attr;        // 指定内存属性
        nonfixed_free_skiplist_init(&mp->free_sl);      // 初始化空闲跳表
        mp->recycle_aq.tid = tid_new();                 // 获取新的tid
        nonfixed_recycle_spsc_atom_queue_init(&mp->recycle_aq.local_recycle_aq);    // 初始化本地回收队列
        nonfixed_recycle_spsc_atom_queue_init(&mp->recycle_aq.remote_recycle_aq);   // 初始化远程回收队列
        nonfixed_recycle_aq_hash_add(&g_nonfixed_recycle_aq_hash, &mp->recycle_aq);    // AQ加入全局哈希表
        // mp加到哈希表中
        nonfixed_mp_hash_add(&g_nonfixed_mp_hash, mp);
    }
}

static void nonfixed_recycle_thd_init()
{
    // 启动后台回收线程
    ev_thd_run(nonfixed_node_recycle);
}

void _mp_nonfixed_supply(mem_type_attr_t *attr)
{
    assert(attr && !mem_type_attr_fixed_size(attr));

    // 获取mp
    nonfixed_mp_t key = {.attr = attr};
    nonfixed_mp_t *mp = nonfixed_mp_hash_find(&g_nonfixed_mp_hash, &key);
    assert(mp);

    // 从系统申请内存，设置边界
    void* chunk = mp_calloc(1, NONFIXED_MP_MEM_SIZE);
    mp->pool_start = chunk;
    mp->pool_end = (char*)chunk + NONFIXED_MP_MEM_SIZE;

    // 初始化首个大节点
    nonfixed_mem_node_t *node = (nonfixed_mem_node_t*)chunk;
    node->attr = attr;
    node->tid = mp->recycle_aq.tid;
    node->size = NONFIXED_MP_MEM_SIZE - sizeof(nonfixed_mem_node_t);        // 实际可使用的要去掉头部
    node->prev_size = 0;        // 第一个节点，没有前驱
    // 链接到空闲跳表
    _mp_nonfixed_men_node_add_fsl(mp, node);

    // 更新申请大小
    ATOM_FETCH_ADD(&attr->allocated, NONFIXED_MP_MEM_SIZE, MORDER_ACQ_REL);
#if MP_DBG_MODE
    ATOM_ADD_FETCH(&mp->attr->piece, 1, MORDER_RELAXED);
#endif
}

static nonfixed_mp_t* _nonfixed_mp_find_or_create(mem_type_attr_t *attr, int supply)
{
    // 查看是否有缓存指针
    if(g_attr_cache == attr && g_nonfixed_mp_cache)
        return g_nonfixed_mp_cache;
    
    // 如果哈希表没初始化，那么调用初始化
    if(!g_nonfixed_mp_hash_init_flag)
    {
        nonfixed_mp_hash_init(&g_nonfixed_mp_hash);
        g_nonfixed_mp_hash_init_flag = 1;
    }

    // 全局哈希表查找
    nonfixed_mp_t key = {.attr = attr};
    nonfixed_mp_t *mp = nonfixed_mp_hash_find(&g_nonfixed_mp_hash, &key);
    if(mp)
    {
        g_attr_cache = attr;
        g_nonfixed_mp_cache = mp;
    }
    else    // 创建并补充节点
    {
        _mp_nonfixed_init(attr);
        if(supply)
            _mp_nonfixed_supply(attr);
        
        mp = nonfixed_mp_hash_find(&g_nonfixed_mp_hash, &key);
        g_attr_cache = attr;
        g_nonfixed_mp_cache = mp;
    }

    return mp;
}

/**
 * @brief       split node for nonfixed mem pool
 * 
 * @param[in]   mp              - mp
 * @param[in]   node_old        - node to split
 * @param[in]   aligned_size    - size
 * 
 * @note        计算从node_old分割出去aligned_size的用户内存后，剩余内存是否满足切割
 */
static nonfixed_mem_node_t* _mp_nonfixed_node_split(nonfixed_mp_t *mp, nonfixed_mem_node_t *node_old, int align_size)
{
    // 计算新节点的可给用户使用的内存
    int remain = node_old->size - align_size - sizeof(nonfixed_mem_node_t);
    if(remain >= NONFIXED_MP_MEM_PIECE) // 超出64B，分割
    {
        // 获取新节点头部位置
        nonfixed_mem_node_t* new_node = 
            (nonfixed_mem_node_t*)((char*)node_old + sizeof(nonfixed_mem_node_t) + align_size);
        // 获取原来的后继的头部位置
        nonfixed_mem_node_t *next = (nonfixed_mem_node_t*)((char*)node_old + sizeof(nonfixed_mem_node_t) + node_old->size);

        // 设置新节点
        new_node->attr = node_old->attr;
        new_node->tid = node_old->tid;
        new_node->size = remain;
        new_node->prev_size = align_size;       // 设置前驱的用户size
        // 旧node的size更改
        node_old->size = align_size;
        // 原先next的prev_size需要修改
        if(_nonfixed_mem_node_valid(mp, next))
            next->prev_size = new_node->size;

#if MP_DBG_MODE
        ATOM_ADD_FETCH(&mp->attr->piece, 1, MORDER_RELAXED);
#endif

        return new_node;
    }
    return NULL;
}

void* _mp_nonfixed_node_get(mem_type_attr_t *attr, int size)
{
    assert(attr && !mem_type_attr_fixed_size(attr) && size);

    // 找到对应的内存池，支持懒初始化
    nonfixed_mp_t *mp = _nonfixed_mp_find_or_create(attr, 1);
    assert(mp);

    // 将local_aq中的内存回收
    nonfixed_mem_node_t *recycle_node = NULL;
    while(recycle_node = nonfixed_recycle_spsc_atom_queue_pop(&mp->recycle_aq.local_recycle_aq))
        _mp_nonfixed_men_node_add_fsl(mp, recycle_node);

    // 将用户传入的size向上按8B对齐，实际分配按照这个来
    size_t aligned_size = aligned_8(size);
    // 获取满足需求的最小节点
    nonfixed_mem_node_t ceil = {.size = aligned_size};
    nonfixed_mem_node_t *node = nonfixed_free_skiplist_ceil(&mp->free_sl, &ceil);
    if(!node)
    {
        dbg_error("mp has no enough memory");
        return NULL;
    }

    // node移出跳表
    _mp_nonfixed_men_node_del_fsl(&mp->free_sl, node);
    // 检查是否进行节点分割
    nonfixed_mem_node_t *new_node = _mp_nonfixed_node_split(mp, node, aligned_size);
    if(new_node)        // 分割的话，将新的节点加入跳表
        _mp_nonfixed_men_node_add_fsl(mp, new_node);

#if MP_DBG_MODE
    int used = node->size;
    if(new_node)    // 切割的话多出一个内存头部占用
        used += sizeof(nonfixed_mem_node_t);
    ATOM_FETCH_ADD(&attr->used, used, MORDER_RELAXED);
#endif

    // 进行清空，再返回给用户data部分
    memset(mp_nonfixed_node_data(node), 0, aligned_size);
    return mp_nonfixed_node_data(node);
}

/**
 * @brief       put node locally
 * 
 * @param[in]   mp      - mem pool
 * @param[in]   node    - mem node
 */
static attr_force_inline void _mp_nonfixed_node_put_local(nonfixed_mp_t *mp, nonfixed_mem_node_t *node)
{
    // 本地回收，直接加回到跳表
    _mp_nonfixed_men_node_add_fsl(mp, node);
}

/**
 * @brief       put node remote ly
 * 
 * @param[in]   mp      - mem pool
 * @param[in]   node    - mem node
 */
static attr_force_inline void _mp_nonfixed_node_put_remote(nonfixed_mp_t *mp, nonfixed_mem_node_t *node)
{
    // 将Node加入线程remote aq
    nonfixed_recycle_spsc_atom_queue_push(&mp->recycle_aq.remote_recycle_aq, node);

    // 超出阈值，唤醒回收线程进行批量回收
    if(nonfixed_recycle_spsc_atom_queue_count(&mp->recycle_aq.remote_recycle_aq) >= RECYCLE_WAKE_THRESHOLD)
        ev_thd_wake(nonfixed_node_recycle);
}

void _mp_nonfixed_node_put(void *ptr)
{
    if(!ptr)
        return;

    // 反推到内存头部
    nonfixed_mem_node_t* node = mp_nonfixed_data_node(ptr);
    assert(node && node->attr && !mem_type_attr_fixed_size(node->attr));     // 检查是否非固定大小

    // 找到对应的内存池，支持懒初始化，不补充内存
    nonfixed_mp_t *mp = _nonfixed_mp_find_or_create(node->attr, 0);
    assert(mp);

    // 检查tid，决定本地回收or跨线程回收
    if(node->tid == mp->recycle_aq.tid)
        _mp_nonfixed_node_put_local(mp, node);
    else
        _mp_nonfixed_node_put_remote(mp, node);

#if MP_DBG_MODE
    // 更新使用数据
    ATOM_FETCH_SUB(&mp->attr->used, node->size, MORDER_RELAXED);
#endif
}

static void _nonfixed_node_recycle_work(void *args)
{
    // 遍历全局aq哈希表，将remote_aq中的内存节点挂到对应tid的local_aq中
    nonfixed_recycle_aq_t *recycle_aq = nonfixed_recycle_aq_hash_first(&g_nonfixed_recycle_aq_hash);
    while(recycle_aq)
    {
        nonfixed_mem_node_t *node = NULL;
        while(node = nonfixed_recycle_spsc_atom_queue_pop(&recycle_aq->remote_recycle_aq))
        {
            nonfixed_recycle_aq_t key = {.tid = node->tid};
            nonfixed_recycle_aq_t *aq = nonfixed_recycle_aq_hash_find(&g_nonfixed_recycle_aq_hash, &key);
            assert(aq);     // 必须有aq
            nonfixed_recycle_spsc_atom_queue_push(&aq->local_recycle_aq, node);
        }
        // 处理下一个aq
        recycle_aq = nonfixed_recycle_aq_hash_next(&g_nonfixed_recycle_aq_hash, recycle_aq);
    }
}

static inline void mp_cli_init()
{
    cli_register("show mp", "dump mem pool info", NULL, _mp_show_mp_hook);
}

static void* _mp_show_mp_hook(unsigned char argc, char *argv[])
{
    unsigned long calloc_size = ATOM_LOAD(&g_mp_calloc_size, MORDER_ACQUIRE);
    unsigned long free_size = ATOM_LOAD(&g_mp_free_size, MORDER_ACQUIRE);
    ATOMIC_UINT32_T total = 0, used = 0;

    // sys calloc

    safe_printf("\nalloc count: %u, size: %luB\n", ATOM_LOAD(&g_mp_calloc_cnt, MORDER_ACQUIRE), calloc_size);
    safe_printf("free  count: %u, size: %luB\n", ATOM_LOAD(&g_mp_free_cnt, MORDER_ACQUIRE), free_size);
    safe_printf("current used: %lu B, %.3f KB\n\n", calloc_size - free_size, (double)(calloc_size - free_size)/1024);

    safe_printf("MP debug mode: [%s], Use-Free-Usage-Piece invalid while close!\n\n", MP_DBG_MODE ? "open" : "close");

    safe_printf(MEM_ATTR_FMT_HEAD MEM_ATTR_FMT_HEAD, 
        "Name", "Type", "Node_Size", "Total", "Use", "Free", "Usage(\%)", "piece",
        "----", "----", "---------", "-----", "---", "----", "--------",  "-----"
    );

    // mem type attr

    mem_type_attr_t *attr = mem_type_attr_hash_first(&g_mem_type_attr_hash_head);
    while(attr)
    {
        total = ATOM_LOAD(&attr->allocated, MORDER_ACQUIRE);
#if MP_DBG_MODE
        used = ATOM_LOAD(&attr->used, MORDER_ACQUIRE);
#endif
        if(mem_type_attr_fixed_size(attr))
#if MP_DBG_MODE
            safe_printf(MEM_ATTR_FIXED_FMT_DATA, attr->name, "fixed", attr->node_size, total, used, total-used, total ? (double)used/total : 0, "/");
#else
            safe_printf(MEM_ATTR_FIXED_FMT_DATA, attr->name, "fixed", attr->node_size, total, 0, 0, 0.0, "/");
#endif
        else
#if MP_DBG_MODE
            safe_printf(MEM_ATTR_NONFIXED_FMT_DATA, attr->name, "nonfixed", "/", total, used, total-used, total ? (double)used/total : 0, ATOM_LOAD(&attr->piece, MORDER_RELAXED));
#else
            safe_printf(MEM_ATTR_NONFIXED_FMT_DATA, attr->name, "nonfixed", "/", total, 0, 0, 0.0, 0);
#endif
        attr = mem_type_attr_hash_next(&g_mem_type_attr_hash_head, attr);
    }

    safe_printf("\n");

    return NULL;
}

/* ========================================================================== */
/*                              Debug Function                                */
/* ========================================================================== */

#if MP_TRACE

void mp_dump_fixed_free_list()
{
    fixed_free_list_t *ffl = NULL;

    printf("dump fixed free list info\n");

    ffl = fixed_free_list_head_hash_first(&g_fixed_free_list_hash_head);
    while(ffl)
    {
        // 查看回收队列情况
        fixed_recycle_aq_t *recycle_aq = &ffl->recycle_aq_head;
        printf("recycle aq: pos %p, tid %u, local aq count %u, remote aq count %u\n",
            recycle_aq, recycle_aq->tid,
            fixed_recycle_spsc_atom_queue_count(&recycle_aq->local_recycle_aq_head),
            fixed_recycle_spsc_atom_queue_count(&recycle_aq->remote_recycle_aq_head)
        );

        // 查看空闲节点
        fixed_free_list_head_t *head = &ffl->head;
        fixed_mem_node_t *node = fixed_free_list_first(head);
        printf("dump fixed free list, count %u\n", fixed_free_list_count(head));
        while(node)
        {
            printf("\tpos %p, attr->name %s, tid %u, size %u, user pos %p\n",
                node, node->attr->name, node->tid, node->size, node->data);
            node = fixed_free_list_next(node);
        }
        printf("========\n");

        ffl = fixed_free_list_head_hash_next(&g_fixed_free_list_hash_head, ffl);
    }
}

void nonfixed_mp_dump()
{
    nonfixed_mp_t *mp = nonfixed_mp_hash_first(&g_nonfixed_mp_hash);
    while(mp)
    {
        // attr信息，tid信息
        safe_printf("=== mp attr name: %s, tid %d ===\n", mp->attr->name, mp->recycle_aq.tid);
        // 打印空闲跳表
        safe_printf("Free SkipList:\n");
        nonfixed_mem_node_t *node = nonfixed_free_skiplist_first(&mp->free_sl);
        while(node)
        {
            // 打印节点信息
            safe_printf("node pos: %p, data pos %p, attr name %s, tid %d, size %d, fl %d, prev_size %d\n", 
                node, mp_nonfixed_node_data(node), node->attr->name, node->tid, node->size, node->fl, node->prev_size);

            // 查找下一个跳表节点
            node = nonfixed_free_skiplist_next(node);
        }
        // 打印aq情况
        safe_printf("----\nLocal aq count: %d, Remote aq count %d\n", 
            nonfixed_recycle_spsc_atom_queue_count(&mp->recycle_aq.local_recycle_aq),
            nonfixed_recycle_spsc_atom_queue_count(&mp->recycle_aq.remote_recycle_aq)
        );

        // 继续查找
        mp = nonfixed_mp_hash_next(&g_nonfixed_mp_hash, mp);
    }
}

#endif