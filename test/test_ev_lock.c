/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    test_ev_lock.c
 * @brief   测试锁
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-03
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-03 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#define _POSIX_C_SOURCE 199309L
#include <pthread.h>
#include "test.h"
#include "event/ev_lock.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define TEST_WRITERS    (2)
#define TEST_READERS    (98)

#define WRITE_TIMES     (100)       // 写次数
#define READ_TIMES      (100000)    // 读次数

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

ev_rwlock_t rwlock = EV_RWLOCK_INITIALIZER;
ev_spinlock_t spinlock = EV_SPINLOCK_INITIALIZER;
unsigned int var = 0;
pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void* writer(void *args)
{
    int i = 0;
    for(i = 0; i < WRITE_TIMES; ++ i)
    {
        ev_with_wrlock(&rwlock)
        {
            ++ var;
        }
    }
}

void* writer_mtx(void *args)
{
    int i = 0;
    for(i = 0; i < WRITE_TIMES; ++ i)
    {
        pthread_mutex_lock(&mtx);
        ++ var;
        pthread_mutex_unlock(&mtx);
    }
}

void* writer_spin(void *args)
{
    int i = 0;
    for(i = 0; i < WRITE_TIMES; ++ i)
    {
        ev_with_spinlock(&spinlock)
        {
            ++ var;
        }
    }
}

void* reader(void *args)
{
    int i = 0;
    unsigned int s_var = 0;

    for(i = 0; i < READ_TIMES; ++ i)
    {
        ev_with_rdlock(&rwlock)
        {
            s_var = var;
        }
    }
}

void* reader_mtx(void *args)
{
    int i = 0;
    unsigned int s_var = 0;

    for(i = 0; i < READ_TIMES; ++ i)
    {
        pthread_mutex_lock(&mtx);
        s_var = var;
        pthread_mutex_unlock(&mtx);
    }
}

void* reader_spin(void *args)
{
    int i = 0;
    unsigned int s_var = 0;

    for(i = 0; i < READ_TIMES; ++ i)
    {
        ev_with_spinlock(&spinlock)
        {
            s_var = var;
        }
    }
}

int test_ev_lock()
{
    pthread_t readers[TEST_READERS] = {};
    pthread_t writers[TEST_WRITERS] = {};
    int i = 0;
    struct timespec s,e;
    unsigned long time;

    test_with_clock(
        s,e,time,
        {
            for(i = 0; i < TEST_WRITERS; ++ i)
            {
                pthread_create(&writers[i], NULL, writer, NULL);
            }
            for(i = 0; i < TEST_READERS; ++ i)
            {
                pthread_create(&readers[i], NULL, reader, NULL);
            }
            for(i = 0; i < TEST_WRITERS; ++ i)
            {
                pthread_join(writers[i], NULL);
            }
            for(i = 0; i < TEST_READERS; ++ i)
            {
                pthread_join(readers[i], NULL);
            }
        }
    );

    assert(var == (WRITE_TIMES * TEST_WRITERS));

    printf("ev_lock: %d readers read %d times, %d writers write %d times, total cost %.3f s\n", 
        TEST_READERS, READ_TIMES, TEST_WRITERS, WRITE_TIMES, (double)time/1e6);

    var = 0;
    test_with_clock(
        s,e,time,
        {
            for(i = 0; i < TEST_WRITERS; ++ i)
            {
                pthread_create(&writers[i], NULL, writer_spin, NULL);
            }
            for(i = 0; i < TEST_READERS; ++ i)
            {
                pthread_create(&readers[i], NULL, reader_spin, NULL);
            }
            for(i = 0; i < TEST_WRITERS; ++ i)
            {
                pthread_join(writers[i], NULL);
            }
            for(i = 0; i < TEST_READERS; ++ i)
            {
                pthread_join(readers[i], NULL);
            }
        }
    );

    assert(var == (WRITE_TIMES * TEST_WRITERS));

    printf("ev_spinlock: %d readers read %d times, %d writers write %d times, total cost %.3f s\n", 
        TEST_READERS, READ_TIMES, TEST_WRITERS, WRITE_TIMES, (double)time/1e6);
    
    var = 0;
    test_with_clock(
        s,e,time,
        {
            for(i = 0; i < TEST_WRITERS; ++ i)
            {
                pthread_create(&writers[i], NULL, writer_mtx, NULL);
            }
            for(i = 0; i < TEST_READERS; ++ i)
            {
                pthread_create(&readers[i], NULL, reader_mtx, NULL);
            }
            for(i = 0; i < TEST_WRITERS; ++ i)
            {
                pthread_join(writers[i], NULL);
            }
            for(i = 0; i < TEST_READERS; ++ i)
            {
                pthread_join(readers[i], NULL);
            }
        }
    );
    assert(var == (WRITE_TIMES * TEST_WRITERS));
    printf("mutex: %d readers read %d times, %d writers write %d times, total cost %.3f s\n", 
        TEST_READERS, READ_TIMES, TEST_WRITERS, WRITE_TIMES, (double)time/1e6);

    return 0;
}