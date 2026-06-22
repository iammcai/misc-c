/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    test_thp.c
 * @brief   测试线程池
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-22
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-22 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "test.h"
#include "plat/atom.h"
#include "thp/thp.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define TEST_THP_DATA_COUNT     (5000)

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

declare_thp(test, 8)

static ATOMIC_UINT64_T sum = 0;
static ATOMIC_UINT32_T i = 0;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static void task(void *args)
{
    uint32_t cur = ATOM_FETCH_ADD(&i, 1, MORDER_ACQ_REL);
    ATOM_FETCH_ADD(&sum, cur, MORDER_ACQ_REL);
}

int test_thp()
{
    // 启动
    thp_run(test);

    unsigned long j = 0;
    for(; j < TEST_THP_DATA_COUNT; ++ j)
        thp_submit_task(test, task, NULL);

    sleep(5);       // 等待执行完毕

    assert(sum == (0+TEST_THP_DATA_COUNT-1)*TEST_THP_DATA_COUNT/2);

    //thp_shutdown(test);

    safe_printf("test thp ok\n");

    return 0;
}