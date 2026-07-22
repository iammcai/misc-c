/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    main.c
 * @brief   项目入口
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-04-28
 * @version 1.0
 *
 * @note    提供项目main函数
 *
 * @history
 *   1.0 | 2026-04-28 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include "test.h"
#include "cli/cli.h"
#include "zcap/zcap.h"
#include "ftx/ftx.h"
#include "syslog/syslog.h"
#include "mp/mp_slab.h"
#include "event/ev_thread.h"
#include "thp/thp.h"
#include "event/ev_timer.h"
#include "event/ev_loop.h"
#include "nt/arp.h"
#include "web/web.h"
#include "nt/ping.h"
#include "nt/tftp.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define TEST_LIST       (0)
#define TEST_HASH       (0)
#define TEST_MP         (0)
#define TEST_MP_SLAB    (0)
#define TEST_AQ         (0)
#define TEST_EV_THD     (0)
#define TEST_EV_LOCK    (0)
#define TEST_MSG_Q      (0)
#define TEST_HEAP       (0)
#define TEST_SKIPLIST   (0)
#define TEST_THP        (0)
#define TEST_EV_TIMER   (0)

/* ========================================================================== */
/*                               Extern Symbols                               */
/* ========================================================================== */

// 声明接口抓包
declare_zcap(eth0)
declare_zcap(wlan0)

// 用于通知platform init ok
ev_sem_t g_platform_init_ok;

/* ========================================================================== */
/*                         Private Function Implementations                   */
/* ========================================================================== */

/**
 * @brief       platform init
 */
static attr_force_inline void _platfrom_init()
{
    /* ----------------------- 显式初始化 ----------------------- */

    ev_sem_init(&g_platform_init_ok);

    debug_level_set(debug_level_all);

    // cli初始化
    cli_module_init();

    // 事件驱动线程框架初始化
    ev_thd_module_init();

    // reactor初始化
    ev_loop_module_init();

    // mem type attr先初始化
    mem_type_attr_module_init();
    
    // 线程池初始化
    thp_module_init();

    // 内存池初始化
    mp_module_init();
    mp_slab_module_init();

    // 时钟初始化
    ev_timer_module_init();

    // debug初始化
    debug_init();

    // 日志初始化
    syslog_module_init();

    // 抓包初始化
    zcap_module_init();
    zcap_register(eth0);
    zcap_register(wlan0);
    // 发包初始化
    ftx_module_init();

    // 启动接口收发包
    zcap_start(eth0);
    declare_ftx(eth0);
    // 启动接口wlan0收发
    zcap_start(wlan0);
    declare_ftx(wlan0);

    // net tools初始化
    arp_module_init();
    ping_module_init();
    tftp_module_init();

    // web初始化
    web_module_init();

    // 关闭debug
    debug_level_set(debug_level_none);

    syslog_notice(SYSLOG_MODULE_SYS, "c-misc system start");

    ev_sem_post(&g_platform_init_ok);
}

int main()
{
    srand(time(NULL));

#if TEST_LIST
    test_type_list();
    test_type_list_cost();
    test_normal_list_cost();
#endif

#if TEST_HASH
    test_type_hash();
#endif

#if TEST_AQ
    //test_aq_spsc();
    //test_normal_queue();
    //test_aq_mpmc();
    test_aq_mpsc();
#endif

#if TEST_EV_THD
    test_ev_thd();
#endif

#if TEST_EV_LOCK
    test_ev_lock();
#endif

#if TEST_MSG_Q
    test_msg_q();
#endif

#if TEST_HEAP
    test_heap();
#endif

#if TEST_SKIPLIST
    //test_skiplist();
    test_skiplist_cost();
#endif

#if TEST_THP
    test_thp();
#endif

#if TEST_EV_TIMER
    //test_ev_timer();
    test_ev_high_res_timer();
#endif

    _platfrom_init();

#if TEST_MP
    //test_mp();
    test_nonfixed_mp_cost();
#endif

#if TEST_MP_SLAB
    //test_slab_mp_function();
    test_slab_mp_cost();
    return 0;
#endif

    // Main进入休眠
    while(1)
    {
        sleep(1);
    }

    return 0;
}