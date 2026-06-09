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

#define TEST_DATA_COUNT     (1)

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

static ATOMIC_UINT64_T mpmc_sum = 0;
static mpmc_atom_queue_head_t *mpmc_aq_head = NULL;

void* mpmc_producer(void *args)
{
    int i = 0;
    
    for(i = 0; i < TEST_DATA_COUNT; ++ i)
    {
        int *data = calloc(1, sizeof(int));
        *data = i;
        mpmc_atom_queue_push(mpmc_aq_head, data);
    }

    return NULL;
}

void* mpmc_consumer(void *args)
{
    int i = 0;

    for(i = 0; i < TEST_DATA_COUNT;)
    {
        int *data = mpmc_atom_queue_pop(mpmc_aq_head);
        if(!data)
            continue;
        ATOM_FETCH_ADD(&mpmc_sum, *data, MORDER_SEQ_SCT);
        free(data);
        ++ i;
    }
}

int test_aq_mpmc()
{
    mpmc_atom_queue_init(&mpmc_aq_head);

    struct timespec s,e;
    unsigned long t;
    pthread_t p1, p2, c2, c1;

    test_with_clock(
        s,e,t,
        {
            pthread_create(&p1, NULL, mpmc_producer, NULL);
            pthread_create(&p2, NULL, mpmc_producer, NULL);
            pthread_create(&c1, NULL, mpmc_consumer, NULL);
            pthread_create(&c2, NULL, mpmc_consumer, NULL);
            pthread_join(p1, NULL);
            pthread_join(p2, NULL);
            pthread_join(c1, NULL);
            pthread_join(c2, NULL);
        }
    )

    assert(mpmc_sum == (unsigned long)(0+TEST_DATA_COUNT-1)*TEST_DATA_COUNT/2*2);

    return 0;
}

#define TEST_MPSC_PRODUCER  (4)
static mpsc_atom_queue_head_t *mpsc_aq_head = NULL;
static unsigned long mpsc_sum = 0;
pre_declare_list(test_mpsc)
typedef struct{
    int data;
    test_mpsc_list_item_t item;
}mpsc_data_t;
declare_list(test_mpsc, test_mpsc, mpsc_data_t, item)
static test_mpsc_list_head_t mpsc_list_head = {};
static mpsc_data_t mpsc_normal_data[TEST_MPSC_PRODUCER][TEST_DATA_COUNT] = {};
static pthread_mutex_t mpsc_normal_mtx = PTHREAD_MUTEX_INITIALIZER;

void* mpsc_normal_producer(void *args)
{
    int i = 0;
    int producer_i = *(int*)args;

    for(i = 0; i < TEST_DATA_COUNT; ++ i)
    {
        pthread_mutex_lock(&mpsc_normal_mtx);
        test_mpsc_list_add_tail(&mpsc_list_head, &mpsc_normal_data[producer_i][i]);
        pthread_mutex_unlock(&mpsc_normal_mtx);
    }
    return NULL;
}

void* mpsc_normal_consumer(void *args)
{
    int i = 0;
    for(i = 0; i < TEST_DATA_COUNT * TEST_MPSC_PRODUCER;)
    {
        pthread_mutex_lock(&mpsc_normal_mtx);
        mpsc_data_t* data = test_mpsc_list_pop(&mpsc_list_head);
        pthread_mutex_unlock(&mpsc_normal_mtx);

        if(!data)
            continue;
        else
        {
            ATOM_FETCH_ADD(&mpsc_sum, data->data, MORDER_RELAXED);
            ++ i;
        }
    }
    return NULL;
}

void* mpsc_producer(void *agrs)
{
    int i = 0;
    for(i = 0; i < TEST_DATA_COUNT; ++ i)
    {
        int *data = malloc(sizeof(int));
        *data = i;
        mpsc_atom_queue_push(mpsc_aq_head, data);
    }
    return NULL;
}

void* mpsc_consumer(void *args)
{
    int i = 0;
    for(i = 0; i < TEST_DATA_COUNT * TEST_MPSC_PRODUCER;)
    {
        int *data = mpsc_atom_queue_pop(mpsc_aq_head);
        if(!data)
            continue;
        else
        {
            ATOM_FETCH_ADD(&mpsc_sum, *data, MORDER_RELAXED);
            free(data);
            ++ i;
        }
    }
    return NULL;
}

int test_aq_mpsc()
{
    pthread_t p1, p2, p3, c1;
    struct timespec s,e;
    unsigned int t;

    mpsc_atom_queue_init(&mpsc_aq_head);

    test_with_clock(
        s,e,t,
        {
            pthread_create(&p1, NULL, mpsc_producer, NULL);
            pthread_create(&p2, NULL, mpsc_producer, NULL);
            pthread_create(&p3, NULL, mpsc_producer, NULL);
            pthread_create(&c1, NULL, mpsc_consumer, NULL);
            pthread_join(p1, NULL);
            pthread_join(p2, NULL);
            pthread_join(p3, NULL);
            pthread_join(c1, NULL);
        }
    )

    assert(mpsc_sum == (unsigned long)TEST_MPSC_PRODUCER*(0+TEST_DATA_COUNT-1)*TEST_DATA_COUNT/2);

    printf("mpsc, producer count %d, push and pop %d items, cost %u us, average %.3f us/item\n",
        TEST_MPSC_PRODUCER, TEST_MPSC_PRODUCER * TEST_DATA_COUNT, t, (double)t/(TEST_MPSC_PRODUCER * TEST_DATA_COUNT));

    test_mpsc_list_init(&mpsc_list_head);
    int i = 0, j = 0;
    for(i = 0; i < TEST_MPSC_PRODUCER; ++ i)
    {
        for(j = 0; j < TEST_DATA_COUNT; ++ j)
            mpsc_normal_data[i][j].data = j;
    }

    mpsc_sum = 0;
    int a0 = 0, a1 = 1, a2 = 2;
    test_with_clock(
        s,e,t,
        {
            pthread_create(&p1, NULL, mpsc_normal_producer, &a0);
            pthread_create(&p2, NULL, mpsc_normal_producer, &a1);
            pthread_create(&p3, NULL, mpsc_normal_producer, &a2);
            pthread_create(&c1, NULL, mpsc_normal_consumer, NULL);
            pthread_join(p1, NULL);
            pthread_join(p2, NULL);
            pthread_join(p3, NULL);
            pthread_join(c1, NULL);
        }
    )
    assert(mpsc_sum == (unsigned long)TEST_MPSC_PRODUCER*(0+TEST_DATA_COUNT-1)*TEST_DATA_COUNT/2);
    printf("mpsc normal queue, producer count %d, push and pop %d items, cost %u us, average %.3f us/item\n",
        TEST_MPSC_PRODUCER, TEST_MPSC_PRODUCER * TEST_DATA_COUNT, t, (double)t/(TEST_MPSC_PRODUCER * TEST_DATA_COUNT));

    return 0;
}