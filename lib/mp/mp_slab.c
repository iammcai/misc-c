/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    mp_slab.c
 * @brief   分级内存池实现
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

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "mp/mp_slab.h"
#include "plat/debug.h"
#include "cli/cli.h"
#include "event/ev_thread.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 根据slab_node获取data
#define slab_node_data(node)    (char*)((char*)node + sizeof(slab_mem_node_t))
// 根据data反推slab_node头部
#define slab_data_node(data)    (slab_mem_node_t*)((char*)data - sizeof(slab_mem_node_t))

// 回收阈值
#define MP_SLAB_RECYCLE_THRESHOLD       (64)

// 回收一批的大小
#define MP_SLAB_RECYCLE_BATCH           (64)

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 内存节点大小数组，与slab_size_e对应
static int g_slab_size_arr[SLAB_SIZE_CNT] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 9216
};
// 内存节点数量数组，与slab_size_e对应
static int g_slab_count_arr[SLAB_SIZE_CNT] = {
    128, 128, 256, 256, 512, 1024, 256, 128, 64, 64, 32
};

// 线程独立哈希表，记录本线程所有slab内存池
static thread_local slab_mp_hash_head_t g_slab_mp_hash = {};
// 标记本线程是否初始化g_slab_mp_hash
static thread_local char g_slab_mp_hash_init_flag = 0;
// 线程内缓存memtype attr
static thread_local mem_type_attr_t *attr_cache = NULL;
// 线程内缓存slab_mp
static thread_local slab_mp_t *slab_mp_cache = NULL;

// 全局recycle_aq哈希表，后台线程遍历回收
static slab_recycle_aq_hash_head_t g_recycle_aq_hash = {};

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       cmp two slab mp
 * 
 * @param[in]   p1  - mp 1
 * @param[in]   p2  - mp 2
 * 
 * @retval      cmp result
 */
static attr_pure_inline int _slab_mp_cmp(slab_mp_t *p1, slab_mp_t *p2)
{
    return strcmp(p1->attr->name, p2->attr->name);
}

/**
 * @brief       hash slab mp
 * 
 * @param[in]   p   - mp
 * 
 * @retval      hash val
 */
static attr_pure_inline unsigned int _slab_mp_hash(slab_mp_t *p)
{
    return type_hash_jhash(p->attr->name, strlen(p->attr->name), 0);
}

/**
 * @brief       cmp two slab recycle struct
 * 
 * @param[in]   a1
 * @param[in]   a2
 * 
 * @retval      cmp result
 */
static attr_pure_inline int _slab_recycle_aq_cmp(slab_recycle_aq_t *a1, slab_recycle_aq_t *a2)
{
    return a1->tid - a2->tid;
}

/**
 * @brief       hash slab recycle struct
 * 
 * @param[in]   a   - recycle struct
 * 
 * @retval      hash val
 */
static attr_pure_inline unsigned int _slab_recycle_aq_hash(slab_recycle_aq_t *a)
{
    return type_hash_jhash(&a->tid, sizeof(tid_t), 0);
}

/**
 * @brief       work function for slab_mp_recycle ev_thd
 * 
 * @note        回收线程工作函数，遍历所有recycle_aq，从remote搬移到local
 */
static void _slab_mp_recycle_work_func(void *args);

// 定义哈希表操作，用于slab内存池哈希
declare_hash(slab_mp, slab_mp, slab_mp_t, item, 1, 31, _slab_mp_cmp, _slab_mp_hash)
// 定义链表操作，用于空闲链表
declare_list(slab_free_mem, slab_free_mem, slab_mem_node_t, item.fl_item)
// 定义链表操作，用于全局回收队列
declare_hash(slab_recycle_aq, slab_recycle_aq, slab_recycle_aq_t, item, 1, 31, _slab_recycle_aq_cmp, _slab_recycle_aq_hash)
// 定义spsc aq操作，用于线程自身回收队列
declare_spsc_atom_queue(slab_recycle, slab_recycle, slab_mem_node_t, item.aq_item)

// 后台线程，来跨线程回收内存，1s一次
declare_ev_thd(slab_mp_recycle, _slab_mp_recycle_work_func, NULL, 1000)

/**
 * @brief       size 2 free list slot
 * 
 * @param[in]   size    - mem node size，没有对齐
 * 
 * @retval      slot of free list array
 * 
 * @note        输入内存大小，计算出空闲链表槽位
 *              需要根据内存分配来修改
 */
static attr_force_inline int _slab_mp_size_2_slot(int size)
{
    uint32_t v = aligned_8(size);

    int slot = 29 - __builtin_clz(v | 1);
    return (v <= 8) ? SLAB_SIZE_8 : slot;
}

/**
 * @brief       find or create slab mp
 * 
 * @param[in]   attr    - mem type attr
 * @param[in]   supply  - if create mp, supply node ?
 * 
 * @retval      slab mp
 */
static attr_force_inline slab_mp_t* _slab_mp_find_or_create(mem_type_attr_t *attr, int supply);

/**
 * @brief       ctor init ev thd
 * 
 * @note        启动回收线程，ev_thd接口使用MID，这里必须比MID等级低
 */
static void mp_slab_ev_thd_init() attr_ctor(CTOR_PRIO_LOW);

/**
 * @brief       ctor init slab mp
 * 
 * @note        构造初始化slab mp相关
 */
static void mp_slab_early_init() attr_ctor(CTOR_PRIO_HIGH);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static void _slab_mp_recycle_work_func(void *args)
{
    slab_recycle_aq_t *aq = slab_recycle_aq_hash_first(&g_recycle_aq_hash);
    while(aq)
    {
        slab_mem_node_t *node = NULL;
        while(node = slab_recycle_spsc_atom_queue_pop(&aq->remote_aq))
        {
            slab_recycle_aq_t key = {.tid = node->tid};
            slab_recycle_aq_t *target_aq = slab_recycle_aq_hash_find(&g_recycle_aq_hash, &key);
            slab_recycle_spsc_atom_queue_push(&target_aq->local_aq, node);
        }
        aq = slab_recycle_aq_hash_next(&g_recycle_aq_hash, aq);
    }
}

void _mp_slab_init(mem_type_attr_t *attr)
{
    // 初始化全局哈希，若有需要
    if(!g_slab_mp_hash_init_flag)
    {
        slab_mp_hash_init(&g_slab_mp_hash);
        g_slab_mp_hash_init_flag = 1;
    }

    // 在线程自身的内存池哈希表中查找是否已经存在该类型内存池
    slab_mp_t key = {.attr = attr};
    slab_mp_t *mp = slab_mp_hash_find(&g_slab_mp_hash, &key);
    if(!mp)     // 没有内存池的话，创建
    {
        mp = mp_calloc(1, sizeof(slab_mp_t));
        mp->attr = attr;

        // 初始化空闲链表
        for(int i = 0; i < SLAB_SIZE_CNT; ++ i)
            slab_free_mem_list_init(&mp->free_lists[i]);

        // 初始化回收队列
        mp->recycle.tid = tid_new();
        slab_recycle_spsc_atom_queue_init(&mp->recycle.local_aq);
        slab_recycle_spsc_atom_queue_init(&mp->recycle.remote_aq);
        slab_recycle_aq_hash_add(&g_recycle_aq_hash, &mp->recycle);

        // mp加入线程局部哈希表
        slab_mp_hash_add(&g_slab_mp_hash, mp);
    }
}

void _mp_slab_supply(mem_type_attr_t *attr)
{
    // 在线程自身的内存池哈希表中查找内存池
    slab_mp_t key = {.attr = attr};
    slab_mp_t *mp = slab_mp_hash_find(&g_slab_mp_hash, &key);

    // 每个链表申请一个大块内存，自行划分加入空闲链表
    for(int i = 0; i < SLAB_SIZE_CNT; ++ i)
    {
        int count = g_slab_count_arr[i];
        if(count == 0)
            continue;
        int size = sizeof(slab_mem_node_t) + g_slab_size_arr[i];
        char *chunk = mp_calloc(count, size);   // 大内存
        for(int j = 0; j < count; ++ j)
        {
            // 初始化属性
            slab_mem_node_t *node = (slab_mem_node_t*)(chunk + j*size);
            node->attr = attr;
            node->slot = i;
            node->tid = mp->recycle.tid;        // 设置tid
            // 加到链表
            slab_free_mem_list_add_head(&mp->free_lists[i], node);
        }
        ATOM_FETCH_ADD(&attr->allocated, count*g_slab_size_arr[i], MORDER_RELAXED);   // 添加统计，不包含头部
#if MP_DBG_MODE
        ATOM_FETCH_ADD(&attr->slab_cnt[i], count, MORDER_RELAXED);
#endif
    }
}

static slab_mp_t* _slab_mp_find_or_create(mem_type_attr_t *attr, int supply)
{
    // 查看是否缓存命中
    if(likely(attr == attr_cache))
        return slab_mp_cache;

    // 如果hash还没初始化，调用
    if(unlikely(!g_slab_mp_hash_init_flag))
    {
        slab_mp_hash_init(&g_slab_mp_hash);
        g_slab_mp_hash_init_flag = 1;
    }
    // 在hash中查找mp
    slab_mp_t key = {.attr = attr};
    slab_mp_t *mp = slab_mp_hash_find(&g_slab_mp_hash, &key);
    if(unlikely(!mp))
    {
        _mp_slab_init(attr);        // 初始化
        if(supply)                  // 补充内存
            _mp_slab_supply(attr);
        mp = slab_mp_hash_find(&g_slab_mp_hash, &key);
    }
    slab_mp_cache = mp;
    attr_cache = attr;

    return mp;
}

/**
 * @brief       fast get node
 * 
 * @param[in]   attr    - attr
 * @param[in]   size    - size
 * 
 * @retval      data
 */
static attr_force_inline void* _mp_slab_node_get_fast(mem_type_attr_t *attr, int size)
{
    // 直接检查cache，初始化等放在slow路径
    if(unlikely(attr != attr_cache))
        return NULL;

    // cache命中，获取内存
    slab_mp_t *mp = slab_mp_cache;
    int slot = _slab_mp_size_2_slot(size);

    slab_mem_node_t *node = slab_free_mem_list_pop(&mp->free_lists[slot]);
    if(likely(node))
    {
#if MP_DBG_MODE
        ATOM_FETCH_ADD(&attr->used, g_slab_size_arr[slot], MORDER_RELAXED);
        ATOM_FETCH_ADD(&attr->slab_used[slot], 1, MORDER_RELAXED);
        ATOM_FETCH_ADD(&attr->hit_total, 1, MORDER_RELAXED);
        ATOM_FETCH_ADD(&attr->slab_hit[slot], 1, MORDER_RELAXED);
#endif
        return (void*)((char*)node + sizeof(slab_mem_node_t));
    }

    return NULL;
}

/**
 * @brief       slow get node
 * 
 * @param[in]   attr    - attr
 * @param[in]   size    - size
 * 
 * @retval      data
 * 
 * @note        慢路径，不做内联，完整走一次
 */
static attr_cold_noinline void* _mp_slab_node_get_slow(mem_type_attr_t *attr, int size)
{
    slab_mp_t *mp = _slab_mp_find_or_create(attr, 1);
    int slot = _slab_mp_size_2_slot(size);

    slab_mem_node_t *node = slab_free_mem_list_pop(&mp->free_lists[slot]);
    if(unlikely(!node))
    {
        // 从 local_aq 回收一批
        int batch = 0;
        slab_mem_node_t *recycle_node;
        while(batch < MP_SLAB_RECYCLE_BATCH &&
               (recycle_node = slab_recycle_spsc_atom_queue_pop(&mp->recycle.local_aq)))
        {
            slab_free_mem_list_add_head(&mp->free_lists[recycle_node->slot], recycle_node);
            batch++;
#if MP_DBG_MODE
            ATOM_FETCH_SUB(&recycle_node->attr->used, recycle_node->slot * g_slab_size_arr[recycle_node->slot], MORDER_RELAXED);
            ATOM_FETCH_SUB(&recycle_node->attr->slab_used[recycle_node->slot], 1, MORDER_RELAXED);
#endif
        }
        // 再次尝试
        node = slab_free_mem_list_pop(&mp->free_lists[slot]);
        if(!node)
        {
            dbg_error("slot %d no memory", slot);
            return NULL;
        }
    }

#if MP_DBG_MODE
    ATOM_FETCH_ADD(&attr->used, g_slab_size_arr[slot], MORDER_RELAXED);
    ATOM_FETCH_ADD(&attr->slab_used[slot], 1, MORDER_RELAXED);
    ATOM_FETCH_ADD(&attr->hit_total, 1, MORDER_RELAXED);
    ATOM_FETCH_ADD(&attr->slab_hit[slot], 1, MORDER_RELAXED);
#endif

    return slab_node_data(node);
}

void* _mp_slab_node_get(mem_type_attr_t *attr, int size)
{
    void *ptr = _mp_slab_node_get_fast(attr, size);
    if(ptr)
        return ptr;

    return _mp_slab_node_get_slow(attr, size);
}

/**
 * @brief       put slab node locally
 * 
 * @param[in]   mp
 * @param[in]   node
 * 
 * @note        本地归还slab node
 */
static attr_force_inline void _mp_slab_node_put_local(slab_mp_t *mp, slab_mem_node_t *node)
{
    // 本地直接加到空闲链表
    slab_free_mem_list_add_head(&mp->free_lists[node->slot], node);

#if MP_DBG_MODE
    ATOM_FETCH_SUB(&node->attr->used, node->slot * g_slab_size_arr[node->slot], MORDER_RELAXED);
    ATOM_FETCH_SUB(&node->attr->slab_used[node->slot], 1, MORDER_RELAXED);
#endif
}

/**
 * @brief       put slab node remotely
 * 
 * @param[in]   mp      - slab mp
 * @param[in]   node    - slab mem node
 * 
 * @note        异地归还slab node
 */
static attr_force_inline void _mp_slab_node_put_remote(slab_mp_t *mp, slab_mem_node_t *node)
{
    // 将node放到本线程的remote_aq，等待后台回收
    slab_recycle_spsc_atom_queue_push(&mp->recycle.remote_aq, node);

    //dbg_always("put remotely");

    // remote_aq堆积则立即唤醒_mp_slab_node_get 
    if(slab_recycle_spsc_atom_queue_count(&mp->recycle.remote_aq) >= MP_SLAB_RECYCLE_THRESHOLD)
        ev_thd_wake(slab_mp_recycle);
}

void _mp_slab_node_put(void *ptr)
{
    if(!ptr)
        return;

    slab_mem_node_t* node = slab_data_node(ptr);     // 假定用户按规范传入mp_slab_node_get返回的指针

    // 找到对应mp，支持懒加载
    slab_mp_t *mp = _slab_mp_find_or_create(node->attr, 0);

    if(node->tid == mp->recycle.tid)
        _mp_slab_node_put_local(mp, node);
    else
        _mp_slab_node_put_remote(mp, node);
}

#if MP_DBG_MODE
static void* _mp_slab_show_cli_hook(unsigned char argc, char *argv[])
{
    mem_type_attr_t *attr = mem_type_attr_first();
    int i = 0;
    safe_printf("\nMP SLAB INFO:\n\n");
    while(attr)
    {
        if(mem_type_attr_slab(attr))
        {
            char str[768] = {};
            snprintf(str, 768, "[%d]. name %s | used /total\n", i, attr->name);
            for(int j = 0; j < SLAB_SIZE_CNT; ++ j)
            {
                snprintf(str+strlen(str), 768-strlen(str), "    slot%-2d (%-4d B): %-4d / %-4d, hit %.3f%%\n", j, g_slab_size_arr[j],
                    ATOM_LOAD(&attr->slab_used[j], MORDER_RELAXED), g_slab_count_arr[j], 
                    (double)ATOM_LOAD(&attr->slab_hit[j], MORDER_RELAXED)/ATOM_LOAD(&attr->hit_total, MORDER_RELAXED) * 100);
            }
            safe_printf("%s\n", str);
            ++ i;
        }
        attr = mem_type_attr_next(attr);
    }
    safe_printf("\n");
    return NULL;
}
#endif

static void mp_slab_ev_thd_init()
{
    ev_thd_run(slab_mp_recycle);
}

static void mp_slab_early_init()
{
    // 初始化全局recycle_aq_hash
    slab_recycle_aq_hash_init(&g_recycle_aq_hash);

#if MP_DBG_MODE
    // 注册调试cli
    cli_register("debug show mp slab", "debug dump slab type mempool", NULL, _mp_slab_show_cli_hook);
#endif
}