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
#include "test.h"
#include "cli/cli.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define TEST_LIST   (0)
#define TEST_HASH   (0)
#define TEST_MP     (0)
#define TEST_AQ     (0)
#define TEST_EV_THD (0)
#define TEST_EV_LOCK    (0)
#define TEST_MSG_Q  (0)

/* ========================================================================== */
/*                               Extern Symbols                               */
/* ========================================================================== */

/* ========================================================================== */
/*                         Private Function Implementations                   */
/* ========================================================================== */

int main()
{
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

    cli_init();     // cli

    // Main进入休眠
    while(1)
    {
        sleep(1);
    }

    return 0;
}