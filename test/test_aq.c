/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    test_aq.c
 * @brief   SPSC原子队列测试文件
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

#define _POSIX_C_SOURCE 199309L
#include <pthread.h>
#include "test.h"
#include "type/type_atom_queue.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

pre_declare_spsc_atom_queue(test)
pre_declare_list(test_normal)

// 测试数据结构定义
typedef struct{
    unsigned int data;
    test_spsc_atom_queue_item_t item;   // item
}data_t;

// 使用普通的队列+锁的数据结构定义
typedef struct{
    unsigned int data;
    test_normal_list_item_t item;   // item
}data_normal_t;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

declare_spsc_atom_queue(test, test, data_t, item)
declare_list(test_normal, test_normal, data_normal_t, item)

#define TEST_DATA_COUNT     (100000)

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

// 测试用aq
static test_spsc_atom_queue_head_t aq_head = {};
static unsigned long aq_sum = 0;

// 测试用normal
static test_normal_list_head_t normal_head = {};
static unsigned long normal_sum = 0;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

/**
 * @brief       atom queue producer
 */
static void* aq_producer(void *agrs)
{
    int i = 0;

    for(i = 0; i < TEST_DATA_COUNT; ++ i)
    {
        data_t *data = (data_t*)calloc(1, sizeof(data_t));
        data->data = i;
        test_spsc_atom_queue_push(&aq_head, data);
    }

    printf("aq_producer work done\n");

    return NULL;
}

/**
 * @brief       atom queue consumer
 */
static void* aq_consumer(void *args)
{
    int i = 0;
    
    for(i = 0; i < TEST_DATA_COUNT; )
    {
        data_t *data = test_spsc_atom_queue_pop(&aq_head);
        if(!data)   // 没获取到，重新尝试
            continue;
        aq_sum += data->data;
        free(data);
        ++ i;
    }

    printf("aq_consumer work done\n");

    return NULL;
}

int test_aq_spsc()
{
    pthread_t producer, consumer;
    struct timespec s, e;
    unsigned int time;

    test_spsc_atom_queue_init(&aq_head);

    test_with_clock(s, e, time, 
        pthread_create(&producer, NULL, aq_producer, NULL);
        pthread_create(&consumer, NULL, aq_consumer, NULL);
        pthread_join(producer, NULL);
        pthread_join(consumer, NULL);
    )

    assert(aq_sum == (unsigned long)(0+TEST_DATA_COUNT-1)*TEST_DATA_COUNT/2);

    printf("test aq spsc PASS\n");
    printf("test push/pop spsc %d items, cost %.3f ms\n", TEST_DATA_COUNT, (double)time/1000);
    printf("========\n");

    return 0;
}

/**
 * @brief       normal producer
 */
static void* normal_producer(void *agrs)
{
    int i = 0;

    for(i = 0; i < TEST_DATA_COUNT; ++ i)
    {
        data_normal_t *data = (data_normal_t*)calloc(1, sizeof(data_normal_t));
        data->data = i;

        pthread_mutex_lock(&mtx);
        test_normal_list_add_tail(&normal_head, data);
        pthread_mutex_unlock(&mtx);
    }

    printf("normal_producer work done\n");

    return NULL;
}

/**
 * @brief       normal consumer
 */
static void* normal_consumer(void *args)
{
    int i = 0;
    
    for(i = 0; i < TEST_DATA_COUNT; )
    {
        pthread_mutex_lock(&mtx);
        if(test_normal_list_count(&normal_head) == 0)
        {
            pthread_mutex_unlock(&mtx);
            continue;
        }
        data_normal_t *data = test_normal_list_pop(&normal_head);
        normal_sum += data->data;
        pthread_mutex_unlock(&mtx);
        free(data);
        ++ i;
    }

    printf("normal_consumer work done\n");

    return NULL;
}

int test_normal_queue()
{
    pthread_t producer, consumer;
    struct timespec s, e;
    unsigned int time;

    test_normal_list_init(&normal_head);

    test_with_clock(s, e, time, 
        pthread_create(&producer, NULL, normal_producer, NULL);
        pthread_create(&consumer, NULL, normal_consumer, NULL);
        pthread_join(producer, NULL);
        pthread_join(consumer, NULL);
    )

    assert(normal_sum == (unsigned long)(0+TEST_DATA_COUNT-1)*TEST_DATA_COUNT/2);

    printf("test normal spsc PASS\n");
    printf("test normal push/pop spsc %d items, cost %.3f ms\n", TEST_DATA_COUNT, (double)time/1000);
    printf("========\n");

    return 0;
}