/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    test_ev_thd.c
 * @brief   事件驱动线程测试文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-02
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-02 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "test.h"
#include "event/ev_thread.h"
#include <unistd.h>

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

static unsigned long p = 0;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

static void producer(void *args)
{
    printf("event thread work! p %u\n", p);
    ++ p;
}

static void ctor1(void)
{
    printf("ctor1 run\n");
}

static void pre1(void)
{
    printf("pre1 run\n");
}

static void post1(void)
{
    printf("post1 run\n");
}

static void dtor1(void)
{
    printf("dtor1 run\n");
}

static void ctor2(void)
{
    printf("ctor2 run\n");
}

static void pre2(void)
{
    printf("pre2 run\n");
}

static void post2(void)
{
    printf("post2 run\n");
}

static void dtor2(void)
{
    printf("dtor2 run\n");
}

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

declare_ev_thd(test_producer, producer, NULL, EV_THD_WAITFOREVER);
declare_ev_thd_ctor(test_producer, ctor1)
declare_ev_thd_prework(test_producer, pre1)
declare_ev_thd_postwork(test_producer, post1)
declare_ev_thd_dtor(test_producer, dtor1)
declare_ev_thd_ctor(test_producer, ctor2)
declare_ev_thd_prework(test_producer, pre2)
declare_ev_thd_postwork(test_producer, post2)
declare_ev_thd_dtor(test_producer, dtor2)

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define TEST_COUNT_MAX  (10)

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

int test_ev_thd()
{
    int i = 0;

    ev_thd_run(test_producer)

    for(i = 0; i < TEST_COUNT_MAX; ++ i)
    {
        ev_thd_wake(test_producer)
        sleep(1);
    }

    ev_thd_cancel(test_producer)

    return 0;
}