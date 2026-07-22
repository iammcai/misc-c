





/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    test_mp_slab.c
 * @brief   测试slab内存池
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-21
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-21 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <pthread.h>
#include "test.h"
#include "mp/mp_slab.h"
#include "msg_q/msg_q.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define TEST_COUNT      (10)

// 测试slab的节点次数
#define TEST_SLAB_NODE_COUNT    (5000)

#define TEST_SLAB_OPS_PER_PRODUCER      TEST_SLAB_NODE_COUNT
#define TEST_SLAB_PRODUCER_COUNT        (4)

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 声明测试内存池
declare_mem_type_slab(test_slab)

declare_mem_type_slab(test_mp_multi)

// 测试功能的数组
static void* ptr[TEST_COUNT] = {};

// 保存指针，测试单线程性能
void* signle_mp_ptrs[TEST_SLAB_NODE_COUNT] = {};
void* signle_sys_ptrs[TEST_SLAB_NODE_COUNT] = {};
// 随机生成内存大小序列
int node_size[TEST_SLAB_NODE_COUNT] = {};

// 多线程测试mp的消息队列
declare_msg_q(test_multi_mp, TEST_SLAB_OPS_PER_PRODUCER*TEST_SLAB_PRODUCER_COUNT, sizeof(void*))
// 多线程测试mp的线程屏障
static pthread_barrier_t barrier_mp;
// 多线程测试sys calloc的消息队列
declare_msg_q(test_multi_sys, TEST_SLAB_OPS_PER_PRODUCER*TEST_SLAB_PRODUCER_COUNT, sizeof(void*))
// 多线程测试sys的线程屏障
static pthread_barrier_t barrier_sys;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static double _test_single_mp_get()
{
    double t;
    struct timespec s,e;
    int i = 0;

    test_with_clock(s,e,t,
        {
            for(int j = 0; j < 10000; ++ j)
                for(i = 0; i < TEST_SLAB_NODE_COUNT; ++ i)
                {
                    signle_mp_ptrs[i] = mp_slab_node_get(test_slab, node_size[i]);
                    if(!signle_mp_ptrs[i])
                        safe_printf("Error occur in mp_slab_node_get\n");
                    mp_slab_node_put(signle_mp_ptrs[i]);
                }
        }
    );

    return t;
}

static double _test_single_malloc()
{
    double t;
    struct timespec s,e;
    int i = 0;

    test_with_clock(s,e,t,
        {
            for(int j = 0; j < 10000; ++ j)
                for(i = 0; i < TEST_SLAB_NODE_COUNT; ++ i)
                {
                    signle_sys_ptrs[i] = malloc(node_size[i]);
                    if(!signle_sys_ptrs[i])
                        safe_printf("Error occur in calloc\n");
                    free(signle_sys_ptrs[i]);  
                }
        }
    );

    return t;
}

// 多线程测试

static void* _test_multi_mp_producer(void *args)
{
    int i = 0;

    // 初始化内存池，补充
    mp_slab_init(test_mp_multi);
    mp_slab_supply(test_mp_multi);

    pthread_barrier_wait(&barrier_mp);      // 等待屏障

    for(i = 0; i < TEST_SLAB_OPS_PER_PRODUCER; ++ i)
    {
        void *ptr = mp_slab_node_get(test_mp_multi, node_size[i]);
        if(!ptr)
           safe_printf("Error occur in mp_slab_node_get\n"); 
        // 推送到消息队列
        msg_q_push(test_multi_mp, &ptr, sizeof(void*));
    }

    return NULL;
}

static void* _test_multi_sys_producer(void *args)
{
    int i = 0;

    pthread_barrier_wait(&barrier_sys);      // 等待屏障

    for(i = 0; i < TEST_SLAB_OPS_PER_PRODUCER; ++ i)
    {
        void *ptr = malloc(node_size[i]);
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

    for(i = 0; i < TEST_SLAB_OPS_PER_PRODUCER * TEST_SLAB_PRODUCER_COUNT; ++ i)
    {
        void *ptr = NULL;
        msg_q_pop(test_multi_mp, sizeof(void*), msg_q_wait_forever, &ptr);
        mp_slab_node_put(ptr);
    }

    return NULL;
}

static void* _test_multi_sys_consumer(void *args)
{
    int i = 0;

    for(i = 0; i < TEST_SLAB_OPS_PER_PRODUCER * TEST_SLAB_PRODUCER_COUNT; ++ i)
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
    pthread_t producer[TEST_SLAB_PRODUCER_COUNT] = {}, consumer;

    // 初始化屏障
    pthread_barrier_init(&barrier_mp, NULL, TEST_SLAB_PRODUCER_COUNT + 1);

    // 创建生产者
    for(i = 0; i < TEST_SLAB_PRODUCER_COUNT; ++ i)
        pthread_create(&producer[i], NULL, _test_multi_mp_producer, NULL);
    
    pthread_barrier_wait(&barrier_mp);      // 等待屏障

    test_with_clock(s,e,t,
        {
            // 创建消费者
            pthread_create(&consumer, NULL, _test_multi_mp_consumer, NULL);

            // 等待结束
            for(i = 0; i < TEST_SLAB_PRODUCER_COUNT; ++ i)
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
    pthread_t producer[TEST_SLAB_PRODUCER_COUNT] = {}, consumer;

    // 初始化屏障
    pthread_barrier_init(&barrier_sys, NULL, TEST_SLAB_PRODUCER_COUNT + 1);
    
    // 创建生产者
    for(i = 0; i < TEST_SLAB_PRODUCER_COUNT; ++ i)
        pthread_create(&producer[i], NULL, _test_multi_sys_producer, NULL);
    
    pthread_barrier_wait(&barrier_sys);      // 等待屏障

    test_with_clock(s,e,t,
        {
            // 创建消费者
            pthread_create(&consumer, NULL, _test_multi_sys_consumer, NULL);

            // 等待结束
            for(i = 0; i < TEST_SLAB_PRODUCER_COUNT; ++ i)
                pthread_join(producer[i], NULL);
            pthread_join(consumer, NULL);
        }
    );

    return t;
}

// 线程归还的入口函数
void *_test_slab_mp_free(void *args)
{
    for(int i = 0; i < TEST_COUNT; ++ i)
        mp_slab_node_put(ptr[i]);
    return NULL;
}

void test_slab_mp_function()
{
    mp_slab_init(test_slab)
    mp_slab_supply(test_slab)

    for(int i = 0; i < TEST_COUNT; ++ i)
    {    
        ptr[i] = mp_slab_node_get(test_slab, 63);
        assert(ptr[i]);
    }

    // 启动线程释放，测试跨线程
    pthread_t tid;
    pthread_create(&tid, NULL, _test_slab_mp_free, NULL);
    pthread_join(tid, NULL);

    // 申请一个内存，触发回收
    sleep(2);
    ptr[0] = mp_slab_node_get(test_slab, 8);
    assert(ptr[0]);
    mp_slab_node_put(ptr[0]);
}

#include <math.h> 
static int rand_normal_centered(int min, int max, double mean, double stddev)
{
    double u1 = ((double)rand() + 1.0) / (RAND_MAX + 2.0);
    double u2 = ((double)rand() + 1.0) / (RAND_MAX + 2.0);
    double z = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2); // Box-Muller
    double val = mean + stddev * z;

    // 截断到 [min, max]
    if (val < min) val = min;
    if (val > max) val = max;
    return (int)(val + 0.5);
}

void test_slab_mp_cost()
{
    safe_printf("\n========== Memory Pool (SLAB) Performance Test ==========\n");
    
    // 初始化内存池并补充节点
    mp_slab_init(test_slab)
    mp_slab_supply(test_slab)
    // 随机序列生成，表示申请内存大小，范围[1, 512]
    for (int i = 0; i < TEST_SLAB_NODE_COUNT; i++)
        node_size[i] = rand_normal_centered(1,512,256,100);

    // 测试单线程mp一次性分配+释放，ping-pong模式，申请-释放交替
    double single_mp_get_pingpong_cost = _test_single_mp_get();
    safe_printf("[1] Alloc + Free, Ping-Pong\r\n");
    safe_printf("    MP: %.3f us, %.3f ops/us\n", single_mp_get_pingpong_cost, (double)TEST_SLAB_NODE_COUNT*10000/single_mp_get_pingpong_cost);
    // 测试单线程sycalloc一次性分配+释放
    double _tsingle_malloc_pingpong_cost = _test_single_malloc();
    safe_printf("    SYS: %.3f us, %.3f ops/us\n", _tsingle_malloc_pingpong_cost, (double)TEST_SLAB_NODE_COUNT*10000/_tsingle_malloc_pingpong_cost);
    safe_printf("    Speedup: %.03f x\n", _tsingle_malloc_pingpong_cost / single_mp_get_pingpong_cost);
#if 0
    // 测试跨线程分配-释放
    safe_printf("--- Multi-thread tests ---\n");
    safe_printf("[2] %d Producers + 1 Consumer\r\n", TEST_SLAB_PRODUCER_COUNT);
    double multi_mp_cost = _test_multi_mp();
    safe_printf("    MP: %.3f us, %.3f ops/us\n", multi_mp_cost, (double)TEST_SLAB_PRODUCER_COUNT*TEST_SLAB_OPS_PER_PRODUCER/multi_mp_cost);
    double multi_sys_cost = _test_multi_sys();
    safe_printf("    SyS: %.3f us, %.3f ops/us\n", multi_sys_cost, (double)TEST_SLAB_PRODUCER_COUNT*TEST_SLAB_OPS_PER_PRODUCER/multi_sys_cost);
    safe_printf("    Speedup: %.03f x\n", multi_sys_cost / multi_mp_cost);
#endif
    safe_printf("==================================================\n");
}