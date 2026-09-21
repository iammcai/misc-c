/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    thp.c
 * @brief   线程池实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-21
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-21 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <string.h>
#include <stdio.h>
#include <sys/prctl.h>

#include "thp/thp.h"
#include "cli/cli.h"
#include "mp/mp_fixed.h"
#include "plat/atom.h"
#include "syslog/syslog.h"
#include "event/ev_lock.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 预定义hash用于记录工作线程数据
pre_declare_hash(thp_worker_stats)

// 记录工作线程工作次数的结构定义
typedef struct{
    pthread_t tid;
    ATOMIC_UINT64_T work_count;     // 工作次数
    thp_worker_stats_hash_item_t item;
}thp_worker_stats_t;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 工作队列使用内存池的大小
#define THP_WORK_COUNT_MAX      (16*4)

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 全局哈希表，记录线程池
static thp_hash_head_t g_thp_hash = {};
// lock
static ev_rwlock_t g_thp_hash_rwlock = EV_RWLOCK_INITIALIZER;

// 全局内存池，用于线程池任务节点
declare_mp_fixed(thp_work_node, THP_WORK_COUNT_MAX, sizeof(thp_work_t))

// 全局工作线程编号
ATOMIC_UINT32_T g_thp_work_thread_id = 0;

// 全局哈希表，记录工作线程数据
static thp_worker_stats_hash_head_t g_thp_worker_stats_hashtable = {};
// lock
static ev_rwlock_t g_thp_worker_stats_hashtable_rwlock = EV_RWLOCK_INITIALIZER;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       cmp two thp_t var
 * 
 * @param[in]   t1  - thp 1
 * @param[in]   t2  - thp 2
 * 
 * @retval      cmp val
 */
static attr_pure_inline int _thp_cmp(const thp_t *t1, const thp_t *t2)
{
    assert(t1 && t1->name && t2 && t2->name);
    return strcmp(t1->name, t2->name);
}

/**
 * @brief       hash thp_t var
 * 
 * @param[in]   t1  - thp 1
 * 
 * @retval      hash val
 */
static attr_pure_inline unsigned int _thp_hash(const thp_t *t1)
{
    assert(t1 && t1->name);
    return type_hash_jhash(t1->name, strlen(t1->name), 0);
}

// 定义哈希表操作
declare_hash(thp, thp, thp_t, item, 1, 31, _thp_cmp, _thp_hash)

/**
 * @brief       cmp two thp_t var
 * 
 * @param[in]   t1  - thp 1
 * @param[in]   t2  - thp 2
 * 
 * @retval      cmp val
 */
static attr_pure_inline int _thp_worker_stats_cmp(const thp_worker_stats_t *t1, const thp_worker_stats_t *t2)
{
    if(t1->tid > t2->tid)
        return 1;
    else if(t1->tid < t2->tid)
        return -1;
    return 0;
}

/**
 * @brief       hash thp_t var
 * 
 * @param[in]   t1  - thp 1
 * 
 * @retval      hash val
 */
static attr_pure_inline unsigned int _thp_worker_stats_hash(const thp_worker_stats_t *t)
{
    return type_hash_jhash(&t->tid, sizeof(pthread_t), 0);
}

// hashtable stats操作定义
declare_hash(thp_worker_stats, thp_worker_stats, thp_worker_stats_t, item, 1, 31, _thp_worker_stats_cmp, _thp_worker_stats_hash)

// 定义工作队列链表操作
declare_list(thp_work, thp_work, thp_work_t, item)

/**
 * @brief       thread in pool routine
 * 
 * @param[in]   args    - thp
 * 
 * @note        线程池内线程的入口
 */
static void* _thp_thread_routine(void *args);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void _thp_init(thp_t *thp)
{
    assert(thp);

    // 初始化工作队列
    thp_work_list_init(&thp->wl);

    // 限制线程数量
    if(thp->thread_count == 0)
        thp->thread_count = 1;
    else if(thp->thread_count > THREAD_POOL_TH_COUNT_MAX)
        thp->thread_count = THREAD_POOL_TH_COUNT_MAX;

    // 加入哈希表
    ev_with_wrlock(&g_thp_hash_rwlock)
        thp_hash_add(&g_thp_hash, thp);
}

void thp_init(thp_t *thp, const char *name, unsigned char thread_count)
{
    assert(thp && name);

    thp->name = strdup(name);
    pthread_mutex_init(&thp->mtx, NULL);
    pthread_cond_init(&thp->cond, NULL);
    thp->thread_count = thread_count;
    thp->shutdown = 1;

    _thp_init(thp);
}

void _thp_run(thp_t *thp)
{
    assert(thp);

    unsigned char expected = 1;
    while (1)
    {
        if(ATOM_CMP_XCHG_WEAK(&thp->shutdown, &expected, 0, MORDER_SEQ_SCT, MORDER_RELAXED))
            break; // CAS 成功
        // CAS 失败，检查 expected 是否变为 0（说明已被其他线程启动）
        if(expected == 0)
        {
            return;
        }
    }
    // 申请线程id数组空间
    thp->thread_array = (pthread_t*)mp_calloc(thp->thread_count, sizeof(pthread_t));
    // 创建工作线程
    unsigned char i = 0;
    for(i = 0; i < thp->thread_count; ++ i)
    {
        int ret = pthread_create(&thp->thread_array[i], NULL, _thp_thread_routine, thp);
        if(0 != ret)
            dbg_error("thp: create thread fail");
        thp_worker_stats_t *stats = mp_calloc(1, sizeof(thp_worker_stats_t));
        stats->tid = thp->thread_array[i];
        stats->work_count = 0;
        ev_with_wrlock(&g_thp_worker_stats_hashtable_rwlock)
            thp_worker_stats_hash_add(&g_thp_worker_stats_hashtable, stats);
    }
}

void* _thp_thread_routine(void *args)
{
    thp_t *thp = (thp_t*)args;

    char buf[16] = {};      // 用来设置name，prctl限制15
    snprintf(buf, sizeof(buf), "thp_thread%d", ATOM_FETCH_ADD(&g_thp_work_thread_id, 1, MORDER_ACQ_REL));
    prctl(PR_SET_NAME, buf);

    while(1)
    {
        pthread_mutex_lock(&thp->mtx);  // lock

        while(0 == thp_work_list_count(&thp->wl) && 0 == ATOM_LOAD(&thp->shutdown, MORDER_ACQUIRE))     // while避免虚假唤醒
            pthread_cond_wait(&thp->cond, &thp->mtx);   // 等待唤醒

        if(1 == ATOM_LOAD(&thp->shutdown, MORDER_ACQUIRE) && 0 == thp_work_list_count(&thp->wl))    // 线程池销毁，退出
        {
            pthread_mutex_unlock(&thp->mtx);
            break;
        }

        // 从任务队列取出任务
        thp_work_t *task = thp_work_list_pop(&thp->wl);

        // 释放锁，再执行任务
        pthread_mutex_unlock(&thp->mtx);

        task->wf(task->args);
        mp_fixed_node_put(task);

        // 添加执行任务次数
        thp_worker_stats_t key = {.tid = pthread_self()};
        thp_worker_stats_t *stats;
        ev_with_rdlock(&g_thp_worker_stats_hashtable_rwlock)
            stats = thp_worker_stats_hash_find(&g_thp_worker_stats_hashtable, &key);
        ATOM_FETCH_ADD(&stats->work_count, 1, MORDER_RELAXED);

        //dbg("thread %d in thp %s, running a task done", (int)pthread_self(), thp->name);
    }

    dbg("thread %d in thp %s running end", (int)pthread_self(), thp->name);

    return NULL;
}

void _thp_submit_task(thp_t *thp, thp_work_func wf, void *args)
{
    assert(thp && wf);

    if(1 == ATOM_LOAD(&thp->shutdown, MORDER_ACQUIRE))
    {
        dbg_error("thp %s has been shutdown", thp->name);
        return;
    }

    thp_work_t *task = mp_fixed_node_get(thp_work_node);    // 尝试获取内存
    if(!task)
    {
        char ctx[SYSLOG_ENTRY_CTX_SIZE] = {};
        snprintf(ctx, sizeof(ctx), "thp %s get task node memory fail, task lose.", thp->name);
        syslog_warn(SYSLOG_MODULE_THP, ctx);
        return;
    }
    memset(task, 0, sizeof(thp_work_t));

    task->wf = wf;
    task->args = args;

    pthread_mutex_lock(&thp->mtx);
    thp_work_list_add_tail(&thp->wl, task);
    pthread_mutex_unlock(&thp->mtx);
    // 唤醒
    pthread_cond_signal(&thp->cond);
}

void _thp_shutdown(thp_t *thp)
{
    if(!thp)
        return;

    // 设置shutdown标志，唤醒所有线程，等待退出
    unsigned char expected = 0;
    if(ATOM_CMP_XCHG_WEAK(&thp->shutdown, &expected, 1, MORDER_SEQ_SCT, MORDER_RELAXED))
    {
        pthread_cond_broadcast(&thp->cond);
        unsigned char i = 0;
        for(; i < thp->thread_count; ++ i)
            pthread_join(thp->thread_array[i], NULL);
        // 释放内存
        free(thp->thread_array);
    }

    dbg("thp %s shutdown", thp->name);
}

unsigned char _thp_is_run(thp_t *thp)
{
    return ATOM_LOAD(&thp->shutdown, MORDER_ACQUIRE) ? 0 : 1;
}

void thp_dump_cli_hook(unsigned char argc, char *argv[])
{
    safe_printf("\n");

    ev_with_rdlock(&g_thp_hash_rwlock)
    {
        int i = 0;
        thp_t *thp = thp_hash_first(&g_thp_hash);
        while(thp)
        {
            safe_printf("thp No.%d, name %s, thread counts %d, state: %s\n", i++, thp->name, thp->thread_count, 
                ATOM_LOAD(&thp->shutdown, MORDER_RELAXED) ? "inactive" : "active");
            for(int j = 0; j < thp->thread_count; ++ j)
            {
                thp_worker_stats_t key = {.tid = thp->thread_array[j]};
                thp_worker_stats_t *stats;
                ev_with_rdlock(&g_thp_worker_stats_hashtable_rwlock)
                    stats = thp_worker_stats_hash_find(&g_thp_worker_stats_hashtable, &key);
                safe_printf("\tthread %d, id %lu, work counts %llu\n", j, thp->thread_array[j], ATOM_LOAD(&stats->work_count, MORDER_RELAXED));
            }
            safe_printf("\n");
            thp = thp_hash_next(&g_thp_hash, thp);
        }
    }

    safe_printf("\n");
}

void thp_module_init(void)
{
    thp_hash_init(&g_thp_hash);
    thp_worker_stats_hash_init(&g_thp_worker_stats_hashtable);

    mem_type_attr_fixed_register(thp_work_node);

    dbg_major("thp module init ok");
}