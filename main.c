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
#include "test.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define TEST_LIST   (0)
#define TEST_HASH   (0)
#define TEST_MP     (1)

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
    test_mp();
    test_mp_cost();
    test_normal_mem_cost();
    test_mp_multi_thread();
#endif

    return 0;
}