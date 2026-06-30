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

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define TEST_LIST       (0)
#define TEST_HASH       (0)
#define TEST_MP         (0)
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

/* ========================================================================== */
/*                         Private Function Implementations                   */
/* ========================================================================== */

/**
 * @brief       platform init
 */
static attr_force_inline void _platfrom_init()
{
    // 开启debug
    debug_level_set(debug_level_all);

    // 声明抓取接口eth0
    declare_zcap(eth0);
    // 启动
    zcap_start(eth0);

    // 声明抓取接口wlan0
    declare_zcap(wlan0);
    // 启动
    zcap_start(wlan0);

    // 初始化cli线程
    cli_init();     // cli

    // 关闭debug
    debug_level_set(debug_level_none);
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

#if TEST_MP
    //test_mp();
    //test_mp_cost();
    //test_normal_mem_cost();
    test_mp_multi_thread();
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

    // Main进入休眠
    while(1)
    {
        sleep(1);
    }

    return 0;
}