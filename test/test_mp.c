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

#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <unistd.h>
#include "test.h"
#include "mp/mp.h"
#include "msg_q/msg_q.h"    // for multi thread

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

/* ==================================== Test Non Fixed ============================================ */

// 测试非定长内存池
declare_mem_type_nonfixed(test_mp_nonfixed)
// 内存指针
static void *p0, *p1, *p2, *p3;

// 消费
static void* _test_mp_nonfixed_consumer(void* args)
{
    mp_nonfixed_node_put(p0);
    mp_nonfixed_node_put(p1);
    mp_nonfixed_node_put(p2);
    mp_nonfixed_node_put(p3);
    return NULL;
}


int test_mp_nonfixed()
{
#if 0
    // 初始化内存池
    mp_nonfixed_init(test_mp_nonfixed)
    mp_nonfixed_supply(test_mp_nonfixed)
#endif

    // 申请内存
    p0 = mp_nonfixed_node_get(test_mp_nonfixed, 64);
    assert(p0);
    p1 = mp_nonfixed_node_get(test_mp_nonfixed, 32);
    assert(p1);
    p2 = mp_nonfixed_node_get(test_mp_nonfixed, 95);
    assert(p2);
    p3 = mp_nonfixed_node_get(test_mp_nonfixed, 256);
    assert(p3);
    // 打印
    nonfixed_mp_dump();

    // 创建线程来归还
    pthread_t consumer;
    pthread_create(&consumer, NULL, _test_mp_nonfixed_consumer, NULL);
    pthread_join(consumer, NULL);
    sleep(2);   // 睡眠1s来让后台归还

    // 再申请一个内存，触发回收
    void *p4 = mp_nonfixed_node_get(test_mp_nonfixed, 512);
    // 打印
    nonfixed_mp_dump();

    return 0;
}

/* ============================== Test Non Fixed MP cost ====================================== */

// 测试性能使用
declare_mem_type_nonfixed(tnf_cost)
// 各种大小内存节点数量
#define MP_NONFIXED_64_COUNT            (10000)
#define MP_NONFIXED_128_COUNT           (5000)
#define MP_NONFIXED_256_COUNT           (1000)
#define MP_NONFIXED_512_COUNT           (500)
// 存储申请来的内存数组
static void* ptr_64[MP_NONFIXED_64_COUNT] = {};
static void* ptr_128[MP_NONFIXED_128_COUNT] = {};
static void* ptr_256[MP_NONFIXED_256_COUNT] = {};
static void* ptr_512[MP_NONFIXED_512_COUNT] = {};

static double _nonfixed_single_test()
{
    double t;
    struct timespec s,e;

    test_with_clock(s,e,t,
        {
            // 分配内存
            for(int i = 0; i < MP_NONFIXED_64_COUNT; ++ i)
            {
                ptr_64[i] = mp_nonfixed_node_get(tnf_cost, 64);
                assert(ptr_64[i]);
            }
            for(int i = 0; i < MP_NONFIXED_128_COUNT; ++ i)
            {
                ptr_128[i] = mp_nonfixed_node_get(tnf_cost, 128);
                assert(ptr_128[i]);
            }
            for(int i = 0; i < MP_NONFIXED_256_COUNT; ++ i)
            {
                ptr_256[i] = mp_nonfixed_node_get(tnf_cost, 256);
                assert(ptr_256[i]);
            }
            for(int i = 0; i < MP_NONFIXED_512_COUNT; ++ i)
            {
                ptr_512[i] = mp_nonfixed_node_get(tnf_cost, 512);
                assert(ptr_512[i]);
            }

            // 归还
            for(int i = 0; i < MP_NONFIXED_512_COUNT; ++ i)
                mp_nonfixed_node_put(ptr_512[i]);
            for(int i = 0; i < MP_NONFIXED_256_COUNT; ++ i)
                mp_nonfixed_node_put(ptr_256[i]);
            for(int i = 0; i < MP_NONFIXED_128_COUNT; ++ i)
                mp_nonfixed_node_put(ptr_128[i]);
            for(int i = 0; i < MP_NONFIXED_64_COUNT; ++ i)
                mp_nonfixed_node_put(ptr_64[i]);
        }
    );

    return t;
}

static double _sys_nonfixed_single_test()
{
    double t;
    struct timespec s,e;

    test_with_clock(s,e,t,
        {
            // 分配内存
            for(int i = 0; i < MP_NONFIXED_64_COUNT; ++ i)
            {
                ptr_64[i] = calloc(1, 64);
                assert(ptr_64[i]);
            }
            for(int i = 0; i < MP_NONFIXED_128_COUNT; ++ i)
            {
                ptr_128[i] = calloc(1, 128);
                assert(ptr_128[i]);
            }
            for(int i = 0; i < MP_NONFIXED_256_COUNT; ++ i)
            {
                ptr_256[i] = calloc(1, 256);
                assert(ptr_256[i]);
            }
            for(int i = 0; i < MP_NONFIXED_512_COUNT; ++ i)
            {
                ptr_512[i] = calloc(1, 512);
                assert(ptr_512[i]);
            }

            // 归还
            for(int i = 0; i < MP_NONFIXED_512_COUNT; ++ i)
                free(ptr_512[i]);
            for(int i = 0; i < MP_NONFIXED_256_COUNT; ++ i)
                free(ptr_256[i]);
            for(int i = 0; i < MP_NONFIXED_128_COUNT; ++ i)
                free(ptr_128[i]);
            for(int i = 0; i < MP_NONFIXED_64_COUNT; ++ i)
                free(ptr_64[i]);
        }
    );

    return t;
}

int test_nonfixed_mp_cost()
{
    safe_printf("\n========== NONFIXED Memory Pool Performance Test ==========\n");

    // 测试单线程mp一次性分配释放
    double nonfixed_single_mp_cost = _nonfixed_single_test();
    safe_printf("--- Single-thread tests ---\n");
    safe_printf("[1] Alloc + Free, Not Ping-Pong\r\n");
    safe_printf("    MP: %.3f us, %.3f ops/us\n", nonfixed_single_mp_cost,
        (double)(MP_NONFIXED_64_COUNT + MP_NONFIXED_128_COUNT + MP_NONFIXED_256_COUNT + MP_NONFIXED_512_COUNT)/nonfixed_single_mp_cost);
    // 测试单线程sys一次性分配释放
    double nonfixed_single_sys_cost = _sys_nonfixed_single_test();
    safe_printf("    SYS: %.3f us, %.3f ops/us\n", nonfixed_single_sys_cost,
        (double)(MP_NONFIXED_64_COUNT + MP_NONFIXED_128_COUNT + MP_NONFIXED_256_COUNT + MP_NONFIXED_512_COUNT)/nonfixed_single_sys_cost);
    safe_printf("    Speedup: %.03f x\n", nonfixed_single_sys_cost / nonfixed_single_mp_cost);
    safe_printf("==================================================\n");

    return 0;
}

/* ============================== Test SLAB MP ====================================== */

