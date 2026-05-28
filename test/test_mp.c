/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    test_mp.c
 * @brief   内存池测试
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-05-28
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-05-28 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#define _POSIX_C_SOURCE 199309L
#include "test.h"
#include "mp/mp.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define TEST_MEM_NODE_SIZE      (512)
#define TEST_MEM_NODE_COUNT     (102400)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

declare_mp_fixed(test_fixed, TEST_MEM_NODE_SIZE, TEST_MEM_NODE_COUNT)

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

int test_normal_mem_cost()
{
    struct timespec s, e;
    int alloc_time, free_time;
    void* ptr[TEST_MEM_NODE_COUNT] = {};
    int i;

    clock_start(s, e, alloc_time)
    for(i = 0; i < TEST_MEM_NODE_COUNT; ++ i)
    {
        ptr[i] = calloc(1, TEST_MEM_NODE_SIZE);
    }
    clock_end(s, e, alloc_time)

    clock_start(s, e, free_time)
    for(i = 0; i < TEST_MEM_NODE_COUNT; ++ i)
    {
        free(ptr[i]);
    }
    clock_end(s, e, free_time)

    printf("test normal mem alloc/free PASS\n");
    printf("get %d nodes by calloc cost %d us, rate %.3f us/n\n", TEST_MEM_NODE_COUNT, alloc_time, (double)alloc_time/TEST_MEM_NODE_COUNT);
    printf("put %d nodes by free cost %d us, rate %.3f us/n\n", TEST_MEM_NODE_COUNT, free_time, (double)free_time/TEST_MEM_NODE_COUNT);
    printf("========\n");

    return 0;
}

int test_mp_cost()
{
    struct timespec s, e;
    int get_time, put_time;
    void* ptr[TEST_MEM_NODE_COUNT] = {};
    int i = 0;

    clock_start(s, e, get_time)
    for(i = 0; i < TEST_MEM_NODE_COUNT; ++ i)
    {
        ptr[i] = mp_fixed_node_get(test_fixed);
        assert(ptr[i]);
    }
    clock_end(s, e, get_time)

    clock_start(s, e, put_time)
    for(i = 0; i < TEST_MEM_NODE_COUNT; ++ i)
    {
        mp_fixed_node_put(ptr[i]);
    }
    clock_end(s, e, put_time)

    printf("test fixed node mp PASS\n");
    printf("get %d nodes from mp cost %d us, rate %.3f us/n\n", TEST_MEM_NODE_COUNT, get_time, (double)get_time/TEST_MEM_NODE_COUNT);
    printf("put %d nodes from mp cost %d us, rate %.3f us/n\n", TEST_MEM_NODE_COUNT, put_time, (double)put_time/TEST_MEM_NODE_COUNT);
    printf("========\n");

    return 0;
}

int test_mp()
{
    mp_dump_fixed_free_list();

    void *p1 = mp_fixed_node_get(test_fixed);
    void *p2 = mp_fixed_node_get(test_fixed);
    void *p3 = mp_fixed_node_get(test_fixed);
    void *p4 = mp_fixed_node_get(test_fixed);
    void *p5 = mp_fixed_node_get(test_fixed);
    void *p6 = mp_fixed_node_get(test_fixed);

    mp_dump_fixed_free_list();

    mp_fixed_node_put(p1);
    mp_fixed_node_put(p2);
    mp_dump_fixed_free_list();

    mp_fixed_node_put(p3);
    mp_fixed_node_put(p4);
    mp_fixed_node_put(p5);
    mp_fixed_node_put(p6);
    mp_dump_fixed_free_list();

    return 0;
}