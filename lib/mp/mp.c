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

// 获取node中的data给用户
#define mp_fixed_node_data(_node)   (_node)->data;
// 根据data获取node
#define mp_fixed_data_node(_data)   container_of(_data, fixed_mem_node_t, data)

// 打印attr的格式 标题：name,node_size,total,use,free,usage(%)
#define MEM_ATTR_FMT_HEAD       "%-15s%-12s%-8s%-8s%-8s%-8s\n"
// 打印attr的格式 数据：name,total,use,free,usage
#define MEM_ATTR_FMT_DATA       "%-15s%-12d%-8d%-8d%-8d%-8.3f\n"

// 表示是否开启调试，开启时会添加计数等，导致性能降低
#define MP_DBG_MODE             (0)

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
static attr_force_inline void _mp_fixed_node_put_remote(fixed_cycle_spsc_atom_queue_head_t *aq_head, fixed_mem_node_t *node);

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

#if MP_DBG_MODE
    // 更新分配计数
    ATOM_FETCH_ADD(&attr->allocated, 1, MORDER_ACQ_REL);
#endif

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
 * @brief       init global variable: g_fixed_cycle_aq_list_head
 * 
 * @note        CTOR
 */
static attr_force_inline void g_fixed_cycle_aq_list_head_init() attr_ctor(CTOR_PRIO_MID);

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
static fixed_cycle_aq_list_head_t g_fixed_cycle_aq_list_head = {};
// 声明回收事件线程，兜底1000ms回收一次
declare_ev_thd(fixed_node_recycle, fixed_node_recycle_work, NULL, 1000)

// 唤醒阈值，线程私有
static thread_local int recycle_wake_thread = 0;

// 全局统计数据
ATOMIC_UINT32_T g_mp_calloc_cnt = 0;     // 申请次数
ATOMIC_UINT64_T g_mp_calloc_size = 0;    // 申请大小
ATOMIC_UINT32_T g_mp_free_cnt = 0;       // 释放次数
ATOMIC_UINT64_T g_mp_free_size = 0;      // 释放大小

// 线程独立的缓存，当前attr
static thread_local mem_type_attr_t *g_attr = NULL;
// 线程独立的缓存，当前free_list
static thread_local fixed_free_list_t *g_fixed_free_list = NULL;


/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

// 定义内存类型属性哈希表相关操作
declare_hash(mem_type_attr, mem_type_attr, mem_type_attr_t, item, 1, 31, _mem_type_attr_cmp, _mem_type_attr_hash)

// 定义空闲链表头哈希表相关操作
declare_hash(fixed_free_list_head, fixed_free_list_head, fixed_free_list_t, item, 1, 31, _fixed_free_list_cmp, _fixed_free_list_hash)

// 定义回收队列链表相关操作
declare_list(fixed_cycle_aq, fixed_cycle_aq, fixed_cycle_aq_t, item)

// 定义回收队列的相关操作
declare_spsc_atom_queue(fixed_cycle, fixed_cycle, fixed_mem_node_t, aq_item)

static inline void g_mem_type_attr_hash_init()
{
    mem_type_attr_hash_init(&g_mem_type_attr_hash_head);
}

static inline void g_fixed_free_list_head_hash_init()
{
    fixed_free_list_head_hash_init(&g_fixed_free_list_hash_head);
}

static inline void g_fixed_cycle_aq_list_head_init()
{
    fixed_cycle_aq_list_init(&g_fixed_cycle_aq_list_head);
}

void mem_type_attr_init(mem_type_attr_t *attr)
{
    assert(attr && attr->name);

    ATOM_STORE(&attr->allocated, 0, MORDER_RELEASE);
    ATOM_STORE(&attr->used, 0, MORDER_RELEASE);

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
        free_list->cycle_aq_head.tid = tid_new();
        fixed_cycle_spsc_atom_queue_init(&free_list->cycle_aq_head.local_cycle_aq_head);
        fixed_cycle_spsc_atom_queue_init(&free_list->cycle_aq_head.remote_cycle_aq_head);
        // 加到全局链表
        fixed_cycle_aq_list_add_tail(&g_fixed_cycle_aq_list_head, &free_list->cycle_aq_head);
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

    if(attr == g_attr)
        return g_fixed_free_list;

    g_attr = attr;

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
    fixed_mem_node_t *cycle_node = NULL;
    while(cycle_node = fixed_cycle_spsc_atom_queue_pop(&free_list->cycle_aq_head.local_cycle_aq_head))
    {
        fixed_free_list_add_head(&free_list->head, cycle_node);
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

static inline void _mp_fixed_node_put_remote(fixed_cycle_spsc_atom_queue_head_t *aq_head, fixed_mem_node_t *node)
{
    // 将node放置到本线程的remote aq中
    fixed_cycle_spsc_atom_queue_push(aq_head, node);

    // 唤醒回收线程
    if(fixed_cycle_spsc_atom_queue_count(aq_head) == RECYCLE_WAKE_THRESHOLD)
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
    if(node->tid == free_list->cycle_aq_head.tid)
        _mp_fixed_node_put_local(free_list, node);
    else
        _mp_fixed_node_put_remote(&free_list->cycle_aq_head.remote_cycle_aq_head, node);

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
    return free_list->cycle_aq_head.tid;
}

static inline fixed_free_list_t* _fixed_free_list_head_hash_find_by_tid(tid_t tid)
{
    fixed_free_list_t *fl = fixed_free_list_head_hash_first(&g_fixed_free_list_hash_head);
    while(fl)
    {
        if(fl->cycle_aq_head.tid == tid)
            return fl;
        fl = fixed_free_list_head_hash_first(&g_fixed_free_list_hash_head);
    }
    return NULL;
}

static inline void fixed_node_recycle_work(void *args)
{
    // 遍历所有aq，逐一将节点挂到正确的aq上
    fixed_cycle_aq_t *aq = fixed_cycle_aq_list_first(&g_fixed_cycle_aq_list_head);
    while(aq)
    {
        fixed_mem_node_t *node = NULL;
        while(node = fixed_cycle_spsc_atom_queue_pop(&aq->remote_cycle_aq_head))
        {
            // 找到对应aq_t，挂到其local队列
            fixed_cycle_aq_t *aq_target = fixed_cycle_aq_list_first(&g_fixed_cycle_aq_list_head);
            while(aq_target)
            {
                if(aq_target->tid == node->tid)
                {
                    fixed_cycle_spsc_atom_queue_push(&aq_target->local_cycle_aq_head, node);
                    break;
                }
                aq_target = fixed_cycle_aq_list_next(aq_target);
            }
            assert(aq_target);
        }
        aq = fixed_cycle_aq_list_next(aq);
    }
}

static inline void fixed_node_recycle_init()
{
    ev_thd_run(fixed_node_recycle);
}

static inline void mp_cli_init()
{
    cli_register("show mp", "dump mem pool info", NULL, _mp_show_mp_hook);
}

static void* _mp_show_mp_hook(unsigned char argc, char *argv[])
{
    unsigned long calloc_size = ATOM_LOAD(&g_mp_calloc_size, MORDER_ACQUIRE);
    unsigned long free_size = ATOM_LOAD(&g_mp_free_size, MORDER_ACQUIRE);


    safe_printf("\nalloc count: %u, size: %luB\n", ATOM_LOAD(&g_mp_calloc_cnt, MORDER_ACQUIRE), calloc_size);
    safe_printf("free  count: %u, size: %luB\n", ATOM_LOAD(&g_mp_free_cnt, MORDER_ACQUIRE), free_size);
    safe_printf("current used: %lu B, %.3f KB\n\n", calloc_size - free_size, (double)(calloc_size - free_size)/1024);

    safe_printf("MP debug mode: [%s], Total-Use-Free-Usage invalid while close!\n\n", MP_DBG_MODE ? "open" : "close");

    safe_printf(MEM_ATTR_FMT_HEAD MEM_ATTR_FMT_HEAD, 
        "Name", "Node_Size", "Total", "Use", "Free", "Usage(\%)",
        "----", "---------", "-----", "---", "----", "--------"
    );

    mem_type_attr_t *attr = mem_type_attr_hash_first(&g_mem_type_attr_hash_head);
    while(attr)
    {
        ATOMIC_UINT32_T total = ATOM_LOAD(&attr->allocated, MORDER_ACQUIRE);
        ATOMIC_UINT32_T used = ATOM_LOAD(&attr->used, MORDER_ACQUIRE);
        safe_printf(MEM_ATTR_FMT_DATA, attr->name, attr->node_size, total, used, total-used, total ?  (double)used/total : 0);
        
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
        fixed_cycle_aq_t *cycle_aq = &ffl->cycle_aq_head;
        printf("cycle aq: pos %p, tid %u, local aq count %u, remote aq count %u\n",
            cycle_aq, cycle_aq->tid,
            fixed_cycle_spsc_atom_queue_count(&cycle_aq->local_cycle_aq_head),
            fixed_cycle_spsc_atom_queue_count(&cycle_aq->remote_cycle_aq_head)
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

#endif