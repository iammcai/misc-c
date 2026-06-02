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
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "mp/mp.h"
#include "plat/atom.h"
#include "event/ev_thread.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define RECYCLE_WAKE_THRESHOLD  (64)   // 唤醒阈值

// 获取node中的data给用户
#define mp_fixed_node_data(_node)   (_node)->data;
// 根据data获取node
#define mp_fixed_data_node(_data)   container_of(_data, fixed_mem_node_t, data)

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
 * @brief       call system calloc
 * 
 * @param[in]   num     - element num
 * @param[in]   size    - elememt size
 * 
 * @retval      ptr to memory
 */
static attr_force_inline void* mp_calloc(size_t num, size_t size)
{
    return calloc(num, size);
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
 * @param[in]   node    - ptr to node
 */
static attr_force_inline void _mp_fixed_node_put_local(fixed_mem_node_t *node);

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
static attr_force_inline void fixed_node_recycle_init() attr_ctor(CTOR_PRIO_HIGH);

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
    mem_type_attr_hash_add(&g_mem_type_attr_hash_head, attr);
}

void _mp_fixed_init(mem_type_attr_t *attr)
{
    // 重要：检查g_fixed_free_list_hash_head是否空，是的话先初始化一下
    {
        fixed_free_list_head_hash_head_t empty = {};
        if(!memcmp(&g_fixed_free_list_hash_head, &empty, sizeof(fixed_free_list_head_hash_head_t)))
            fixed_free_list_head_hash_init(&g_fixed_free_list_hash_head);
    }

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

    // 补充节点数量到需求
    fixed_free_list_supply(&free_list->head, attr);
}

void fixed_free_list_supply(fixed_free_list_head_t *head, mem_type_attr_t *attr)
{
    int count = 0;
    fixed_mem_node_t *node = NULL;

    assert(head && attr);

    while(fixed_free_list_count(head) < attr->node_max_num)
    {
        node = fixed_mem_node_new(attr);
        fixed_free_list_add_head(head, node);
    }
}

void* _mp_fixed_node_get(mem_type_attr_t *attr)
{
    if(!mem_type_attr_fixed_size(attr))     // 检查是否固定大小
        return NULL;

    // 找到free list
    fixed_free_list_t key = {.attr = attr};
    fixed_free_list_t *free_list = fixed_free_list_head_hash_find(&g_fixed_free_list_hash_head, &key);
    assert(free_list);

    // 将local aq中的节点都归还回来
    while(fixed_cycle_spsc_atom_queue_count(&free_list->cycle_aq_head.local_cycle_aq_head))
    {
        fixed_mem_node_t *node = fixed_cycle_spsc_atom_queue_pop(&free_list->cycle_aq_head.local_cycle_aq_head);
        fixed_free_list_add_head(&free_list->head, node);
    }

    if(!fixed_free_list_count(&free_list->head))    // 检查空闲节点个数
        return NULL;

    fixed_mem_node_t *node = fixed_free_list_pop(&free_list->head); // pop一个节点

    return mp_fixed_node_data(node);
}

static inline void _mp_fixed_node_put_local(fixed_mem_node_t *node)
{
    fixed_free_list_t key = {.attr = node->attr};
    fixed_free_list_t *free_list = fixed_free_list_head_hash_find(&g_fixed_free_list_hash_head, &key);  // 找到freelist
    assert(free_list);

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
    assert(mem_type_attr_fixed_size(node->attr));       // 确认是否fixed

    fixed_free_list_t key = {.attr = node->attr};
    fixed_free_list_t *free_list = fixed_free_list_head_hash_find(&g_fixed_free_list_hash_head, &key);  // 找到freelist

    // 本地归还 or 异地归还
    if(node->tid == free_list->cycle_aq_head.tid)
        _mp_fixed_node_put_local(node);
    else
        _mp_fixed_node_put_remote(&free_list->cycle_aq_head.remote_cycle_aq_head, node);
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