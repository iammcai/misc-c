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
#include "thp/thp.h"
#include "cli/cli.h"
#include "mp/mp.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 工作队列使用内存池的大小
#define THP_WORK_COUNT_MAX      (64*4)

// 打印格式：name count state
#define THP_DUMP_HEAD_DMT   "%-12s%-8s%-12s\n"
#define THP_DUMP_FMT        "%-12s%-8d%-12s\n"

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 全局哈希表，记录线程池
static thp_hash_head_t g_thp_hash = {};

// 全局内存池，用于线程池任务节点
declare_mem_type_fixed(thp_work_node, sizeof(thp_work_t), THP_WORK_COUNT_MAX)

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

/**
 * @brief       show all thp, for cli: show thread pool
 * 
 * @note        打印所有thp的cli回调
 */
static void* _thp_dump_cli_hook(unsigned char argc, char *argv[]);

/**
 * @brief       thread pool init ctor
 * 
 * @note        初始化哈希表，注册cli
 */
static void thp_init_early(void) attr_ctor(CTOR_PRIO_MID);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void _thp_init(thp_t *thp)
{
    assert(thp);

    // 初始化工作队列
    thp_work_list_init(&thp->wl);

    // 限制线程数量
    if(thp->thread_count <= 0)
        thp->thread_count = 1;
    else if(thp->thread_count > THREAD_POOL_TH_COUNT_MAX)
        thp->thread_count = THREAD_POOL_TH_COUNT_MAX;

    // 加入哈希表
    thp_hash_add(&g_thp_hash, thp);
}

void thp_init(thp_t *thp, const char *name, unsigned char thread_count)
{
    assert(thp && name);

    thp->name = mp_strdup(name);
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
    if(ATOM_CMP_XCHG_WEAK(&thp->shutdown, &expected, 0, MORDER_SEQ_SCT, MORDER_RELAXED))
    {
        // 申请线程id数组空间
        thp->thread_array = (pthread_t*)mp_calloc(thp->thread_count, sizeof(pthread_t));
        // 创建工作线程
        unsigned char i = 0;
        for(i = 0; i < thp->thread_count; ++ i)
            assert(-1 != pthread_create(&thp->thread_array[i], NULL, _thp_thread_routine, thp));
    }
}

void* _thp_thread_routine(void *args)
{
    thp_t *thp = (thp_t*)args;

    // dbg("thread %d in thp %s start running", (int)pthread_self(), thp->name);

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

    thp_work_t *task;
    do{
        task = mp_fixed_node_get(thp_work_node);    // 尝试获取内存
    }while(!task);

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
        mp_free(thp->thread_array, thp->thread_count * sizeof(pthread_t));
    }

    dbg("thp %s shutdown", thp->name);
}

unsigned char _thp_is_run(thp_t *thp)
{
    return ATOM_LOAD(&thp->shutdown, MORDER_ACQUIRE) ? 0 : 1;
}

static void* _thp_dump_cli_hook(unsigned char argc, char *argv[])
{
    safe_printf("\n");
    safe_printf(THP_DUMP_HEAD_DMT, "thp_name", "th_cnt", "state");
    safe_printf(THP_DUMP_HEAD_DMT, "--------", "------", "-----");
    thp_t *thp = thp_hash_first(&g_thp_hash);
    while(thp)
    {
        safe_printf(THP_DUMP_FMT, thp->name, thp->thread_count, ATOM_LOAD(&thp->shutdown, MORDER_RELAXED) ? "shutdown" : "active");
        thp = thp_hash_next(&g_thp_hash, thp);
    }
    safe_printf("\n");
}

static void thp_init_early(void)
{
    thp_hash_init(&g_thp_hash);

    cli_register("show thread pool", "show all thread pools info", NULL, _thp_dump_cli_hook);
}