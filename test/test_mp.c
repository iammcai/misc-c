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

// 测试用的内存结点大小
#define TEST_FIXED_NODE_SIZE        (1600)
// 测试内存节点数量
#define TEST_FIXED_NODE_COUNT       (200000)

// 多线程生产者个数
#define TEST_FIXED_PRODUCER_COUNT   (3)
// 每个线程生产个数
#define TEST_FIXED_OPS_PER_PRODUCER (200000)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

// 测试使用的内存池
declare_mem_type_fixed(test_mp, TEST_FIXED_NODE_SIZE, TEST_FIXED_NODE_COUNT)
// 多线程使用
declare_mem_type_fixed(test_mp_multi, TEST_FIXED_NODE_SIZE, TEST_FIXED_OPS_PER_PRODUCER)

// mp单线程测试分配的指针
static void* signle_mp_ptrs[TEST_FIXED_NODE_COUNT] = {};
// calloc单线程测试
static void* signle_sys_ptrs[TEST_FIXED_NODE_COUNT] = {};

// 多线程测试mp的消息队列
declare_msg_q(test_multi_mp, TEST_FIXED_OPS_PER_PRODUCER*TEST_FIXED_PRODUCER_COUNT, sizeof(void*))
// 多线程测试mp的线程屏障
static pthread_barrier_t barrier_mp;
// 多线程测试sys calloc的消息队列
declare_msg_q(test_multi_sys, TEST_FIXED_OPS_PER_PRODUCER*TEST_FIXED_PRODUCER_COUNT, sizeof(void*))
// 多线程测试sys的线程屏障
static pthread_barrier_t barrier_sys;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static double _test_single_mp_get(int ping_pong)
{
    double t;
    struct timespec s,e;
    int i = 0;

    test_with_clock(s,e,t,
        {
            for(i = 0; i < TEST_FIXED_NODE_COUNT; ++ i)
            {
                signle_mp_ptrs[i] = mp_fixed_node_get(test_mp);
                if(!signle_mp_ptrs[i])
                    safe_printf("Error occur in mp_fixed_node_get\n");
                if(ping_pong)
                    mp_fixed_node_put(signle_mp_ptrs[i]);
            }

            if(!ping_pong)
            {
                for(i = 0; i < TEST_FIXED_NODE_COUNT; ++ i)
                    mp_fixed_node_put(signle_mp_ptrs[i]);
            }
              
        }
    );

    return t;
}

static double _test_single_calloc(int ping_pong)
{
    double t;
    struct timespec s,e;
    int i = 0;

    test_with_clock(s,e,t,
        {
            for(i = 0; i < TEST_FIXED_NODE_COUNT; ++ i)
            {
                signle_sys_ptrs[i] = calloc(1, TEST_FIXED_NODE_SIZE);
                if(!signle_sys_ptrs[i])
                    safe_printf("Error occur in calloc\n");
                if(ping_pong)
                    free(signle_sys_ptrs[i]);  
            }

            if(!ping_pong)
            {
                for(i = 0; i < TEST_FIXED_NODE_COUNT; ++ i)
                    free(signle_sys_ptrs[i]);  
            }
        }
    );

    return t;
}

static void* _test_multi_mp_producer(void *args)
{
    int i = 0;

    // 初始化内存池，补充
    mp_fixed_init(test_mp_multi);
    mp_fixed_supply(test_mp_multi);

    pthread_barrier_wait(&barrier_mp);      // 等待屏障

    for(i = 0; i < TEST_FIXED_OPS_PER_PRODUCER; ++ i)
    {
        void *ptr = mp_fixed_node_get(test_mp_multi);
        if(!ptr)
           safe_printf("Error occur in mp_fixed_node_get\n"); 
        // 推送到消息队列
        msg_q_push(test_multi_mp, &ptr, sizeof(void*));
    }

    return NULL;
}

static void* _test_multi_sys_producer(void *args)
{
    int i = 0;

    pthread_barrier_wait(&barrier_sys);      // 等待屏障

    for(i = 0; i < TEST_FIXED_OPS_PER_PRODUCER; ++ i)
    {
        void *ptr = calloc(1, TEST_FIXED_NODE_SIZE);
        if(!ptr)
           safe_printf("Error occur in calloc\n"); 
        // 推送到消息队列
        msg_q_push(test_multi_sys, &ptr, sizeof(void*));
    }

    return NULL;
}

static void* _test_multi_mp_consumer(void *args)
{
    int i = 0;

    for(i = 0; i < TEST_FIXED_OPS_PER_PRODUCER * TEST_FIXED_PRODUCER_COUNT; ++ i)
    {
        void *ptr = NULL;
        msg_q_pop(test_multi_mp, sizeof(void*), msg_q_wait_forever, &ptr);
        mp_fixed_node_put(ptr);
    }

    return NULL;
}

static void* _test_multi_sys_consumer(void *args)
{
    int i = 0;

    for(i = 0; i < TEST_FIXED_OPS_PER_PRODUCER * TEST_FIXED_PRODUCER_COUNT; ++ i)
    {
        void *ptr = NULL;
        msg_q_pop(test_multi_sys, sizeof(void*), msg_q_wait_forever, &ptr);
        free(ptr);
    }

    return NULL;
}

static double _test_multi_mp()
{
    double t;
    struct timespec s,e;
    int i = 0;
    pthread_t producer[TEST_FIXED_PRODUCER_COUNT] = {}, consumer;

    // 初始化屏障
    pthread_barrier_init(&barrier_mp, NULL, TEST_FIXED_PRODUCER_COUNT + 1);

    // 创建生产者
    for(i = 0; i < TEST_FIXED_PRODUCER_COUNT; ++ i)
        pthread_create(&producer[i], NULL, _test_multi_mp_producer, NULL);
    
    pthread_barrier_wait(&barrier_mp);      // 等待屏障

    test_with_clock(s,e,t,
        {
            // 创建消费者
            pthread_create(&consumer, NULL, _test_multi_mp_consumer, NULL);

            // 等待结束
            for(i = 0; i < TEST_FIXED_PRODUCER_COUNT; ++ i)
                pthread_join(producer[i], NULL);
            pthread_join(consumer, NULL);
        }
    );

    return t;
}

static double _test_multi_sys()
{
    double t;
    struct timespec s,e;
    int i = 0;
    pthread_t producer[TEST_FIXED_PRODUCER_COUNT] = {}, consumer;

    // 初始化屏障
    pthread_barrier_init(&barrier_sys, NULL, TEST_FIXED_PRODUCER_COUNT + 1);
    
    // 创建生产者
    for(i = 0; i < TEST_FIXED_PRODUCER_COUNT; ++ i)
        pthread_create(&producer[i], NULL, _test_multi_sys_producer, NULL);
    
    pthread_barrier_wait(&barrier_sys);      // 等待屏障

    test_with_clock(s,e,t,
        {
            // 创建消费者
            pthread_create(&consumer, NULL, _test_multi_sys_consumer, NULL);

            // 等待结束
            for(i = 0; i < TEST_FIXED_PRODUCER_COUNT; ++ i)
                pthread_join(producer[i], NULL);
            pthread_join(consumer, NULL);
        }
    );

    return t;
}

int test_mp()
{
    safe_printf("\n========== Memory Pool Performance Test ==========\n");
    
    // 初始化内存池并补充节点
    mp_fixed_init(test_mp)
    mp_fixed_supply(test_mp)

    // 测试单线程mp一次性分配+释放
    double single_mp_get_cost = _test_single_mp_get(0);
    safe_printf("--- Single-thread tests ---\n");
    safe_printf("[1] Alloc + Free, Not Ping-Pong\r\n");
    safe_printf("    MP: %.3f us, %.3f ops/us\n", single_mp_get_cost, (double)TEST_FIXED_NODE_COUNT/single_mp_get_cost);
    // 测试单线程sycalloc一次性分配+释放
    double single_calloc_cost = _test_single_calloc(0);
    safe_printf("    SYS: %.3f us, %.3f ops/us\n", single_calloc_cost, (double)TEST_FIXED_NODE_COUNT/single_calloc_cost);
    safe_printf("    Speedup: %.03f x\n", single_calloc_cost / single_mp_get_cost);

    // 测试单线程mp一次性分配+释放，ping-pong模式，申请-释放交替
    double single_mp_get_pingpong_cost = _test_single_mp_get(1);
    safe_printf("[2] Alloc + Free, Ping-Pong\r\n");
    safe_printf("    MP: %.3f us, %.3f ops/us\n", single_mp_get_pingpong_cost, (double)TEST_FIXED_NODE_COUNT/single_mp_get_pingpong_cost);
    // 测试单线程sycalloc一次性分配+释放
    double single_calloc_pingpong_cost = _test_single_calloc(1);
    safe_printf("    SYS: %.3f us, %.3f ops/us\n", single_calloc_pingpong_cost, (double)TEST_FIXED_NODE_COUNT/single_calloc_pingpong_cost);
    safe_printf("    Speedup: %.03f x\n", single_calloc_pingpong_cost / single_mp_get_pingpong_cost);

    // 测试跨线程分配-释放
    safe_printf("--- Multi-thread tests ---\n");
    safe_printf("[3] %d Producers + 1 Consumer\r\n", TEST_FIXED_PRODUCER_COUNT);
    double multi_mp_cost = _test_multi_mp();
    safe_printf("    MP: %.3f us, %.3f ops/us\n", multi_mp_cost, (double)TEST_FIXED_PRODUCER_COUNT*TEST_FIXED_OPS_PER_PRODUCER/multi_mp_cost);
    double multi_sys_cost = _test_multi_sys();
    safe_printf("    SyS: %.3f us, %.3f ops/us\n", multi_sys_cost, (double)TEST_FIXED_PRODUCER_COUNT*TEST_FIXED_OPS_PER_PRODUCER/multi_sys_cost);
    safe_printf("    Speedup: %.03f x\n", multi_sys_cost / multi_mp_cost);

    safe_printf("==================================================\n");

    return 0;
}

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