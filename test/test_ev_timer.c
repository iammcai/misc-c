/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    test_ev_timer.c
 * @brief   测试定时器
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-05-29
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-05-29 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "test.h"
#include "event/ev_timer.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void test_timer_cb(void *args)
{
    dbg_always("timer expired!!");
}

int test_ev_timer()
{
    unsigned char i = 5;
    ev_timer_t *timer = NULL;
    for(; i < 10; ++ i)
    {
        timer = ev_timer_create(i*1000, test_timer_cb, NULL);
        ev_timer_start(timer);
    }
    ev_timer_stop(timer);

    sleep(11);
    return 0;
}

int test_ev_high_res_timer()
{
    unsigned char i = 1;
    ev_high_res_timer_t *timer;

    for(; i < 5; ++ i)
    {
        char name[10];
        snprintf(name, 10, "hrt_%d", i);
        timer = ev_high_res_timer_create(name, i*1000, test_timer_cb, NULL);
        ev_high_res_timer_start(timer);
    }
    ev_high_res_timer_stop(timer);

    sleep(5);

    // 重新启动timer
    ev_high_res_timer_start(timer);

    return 0;
}