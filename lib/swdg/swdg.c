/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    swdg.c
 * @brief   软件看门狗实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-08-27
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-08-27 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdio.h>
#include <pthread.h>

#include "swdg/swdg.h"
#include "plat/atom.h"
#include "plat/compiler.h"
#include "plat/debug.h"
#include "type/type_hash.h"
#include "mp/mp.h"
#include "event/ev_lock.h"
#include "event/ev_timer.h"
#include "event/ev_thread.h"
#include "syslog/syslog.h"
#include "cli/cli.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 后台监控线程周期
#define SWDG_MONITOR_INTERVAL       (2000)

// cli打印格式，tid, name, threshold, since_last_feed
#define SWDG_FMT_HEAD       "%-18s%-32s%-16s%-16s\n"
#define SWDG_FMT_DATA       "%-18lu%-32s%-16lu%-16lu\n"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 预定义哈希表用于存储线程状态
pre_declare_hash(swdg_thd_stats)

// 线程状态定义
typedef struct{
    pthread_t tid;          // 唯一索引，线程ID
    char *name;             // 线程名

    ATOMIC_UINT64_T last_feed;      // 上次喂狗的时间
    uint64_t threshold;             // 超时阈值

    swdg_thd_stats_hash_item_t item;    // item
}swdg_thd_stats_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       cmp stats
 */
static attr_pure_inline int _swdg_thd_stats_cmp(swdg_thd_stats_t *st1, swdg_thd_stats_t *st2)
{
    return st1->tid - st2->tid;
}

/**
 * @brief       hash stats
 */
static attr_pure_inline unsigned int _swdg_thd_stats_hash(swdg_thd_stats_t *st)
{
    return type_hash_jhash(&st->tid, sizeof(pthread_t), 0);
}

// 定义hash操作
declare_hash(swdg_thd_stats, swdg_thd_stats, swdg_thd_stats_t, item, 1, 31, _swdg_thd_stats_cmp, _swdg_thd_stats_hash)

/**
 * @brief       swdg monitor ev thd work function
 */
static void _swdg_monitor_wf(void *args);

/**
 * @brief       cli cb for: show swdg
 */
static void* _swdg_show_swdg_cli_cb(unsigned char argc, char *argv[]);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 全局哈希表，管理所有线程喂狗信息
static swdg_thd_stats_hash_head_t g_swdg_thd_stats_hashtable = {};
// 哈希表锁
static ev_rwlock_t g_swdg_thd_stats_hash_rwlock = EV_RWLOCK_INITIALIZER;

// 后台监控线程
declare_ev_thd(swdg_monitor, _swdg_monitor_wf, NULL, SWDG_MONITOR_INTERVAL);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static void _swdg_monitor_wf(void *args)
{
    uint64_t curr = low_res_cur_time_get();

    dbg("swdg monitor wake");

    ev_with_rdlock(&g_swdg_thd_stats_hash_rwlock)
    {
        swdg_thd_stats_t *st = swdg_thd_stats_hash_first(&g_swdg_thd_stats_hashtable);
        while(st)
        {
            if(unlikely(curr - ATOM_LOAD(&st->last_feed, MORDER_ACQUIRE) > st->threshold))
            {
                char ctx[SYSLOG_ENTRY_CTX_SIZE] = {};
                snprintf(ctx, sizeof(ctx), "thd %s, tid %d, dog need feed!!", st->name, st->tid);
                syslog_warn(SYSLOG_MODULE_SWDG, ctx);
            }
            st = swdg_thd_stats_hash_next(&g_swdg_thd_stats_hashtable, st);
        }
    }
}

void _swdg_register(const char *name, uint64_t threshold)
{
    swdg_thd_stats_t *stat = mp_calloc(1, sizeof(swdg_thd_stats_t));
    if(unlikely(!stat))
        return;

    stat->name = mp_strdup(name);
    ATOM_STORE(&stat->last_feed, low_res_cur_time_get(), MORDER_RELEASE);   // 获取当前时间，低精度时钟源
    stat->threshold = threshold;
    stat->tid = pthread_self();

    // 加入哈希表
    ev_with_wrlock(&g_swdg_thd_stats_hash_rwlock)
        swdg_thd_stats_hash_add(&g_swdg_thd_stats_hashtable, stat);
}

void _swdg_feed()
{
    // 找到对应的状态变量
    swdg_thd_stats_t key = {.tid = pthread_self()};
    swdg_thd_stats_t *st = NULL;

    ev_with_wrlock(&g_swdg_thd_stats_hash_rwlock)
    {
        st = swdg_thd_stats_hash_find(&g_swdg_thd_stats_hashtable, &key);
        if(unlikely(!st))
        {
            dbg_error("feed wdg fail, cant find");
            return;
        }
        // 更新喂狗时间
        ATOM_STORE(&st->last_feed, low_res_cur_time_get(), MORDER_RELEASE);
    }
}

static void* _swdg_show_swdg_cli_cb(unsigned char argc, char *argv[])
{
    uint64_t curr = low_res_cur_time_get();

    safe_printf("\n                              software watch dog\n" 
        "---------------------------------------------------------------------------------\n"
        SWDG_FMT_HEAD SWDG_FMT_HEAD, 
        "tid", "name", "threshold", "since_last_feed",
        "---", "----", "---------", "---------------");
    ev_with_rdlock(&g_swdg_thd_stats_hash_rwlock)
    {
        swdg_thd_stats_t *st = swdg_thd_stats_hash_first(&g_swdg_thd_stats_hashtable);
        while(st)
        {
            safe_printf(SWDG_FMT_DATA, st->tid, st->name, st->threshold, curr - ATOM_LOAD(&st->last_feed, MORDER_ACQUIRE));
            st = swdg_thd_stats_hash_next(&g_swdg_thd_stats_hashtable, st);
        }
    }
    safe_printf("---------------------------------------------------------------------------------\n\n");

    return NULL;
}

void swdg_module_init_th()
{
    swdg_thd_stats_hash_init(&g_swdg_thd_stats_hashtable);

    dbg_major("swdg module init tf ok");
}

void swdg_module_init_bh()
{
    ev_thd_register(swdg_monitor);
    ev_thd_run(swdg_monitor);

    cli_register("show swdg", "show software watch dog status", NULL, _swdg_show_swdg_cli_cb);

    dbg_major("swdg module init bh ok");
}