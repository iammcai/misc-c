/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    test.h
 * @brief   测试
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-05-26
 * @version 1.0
 *
 * @note
 *
 * @history
 *   1.0 | 2026-05-26 | cai | Initial creation.
 */

#ifndef __TEST_H__
#define __TEST_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "type/type_list.h"
#include "plat/debug.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// s,e struct timespec type ; t, unsigned int type, us
#define clock_start(s, e, t)    \
    {   \
        clock_gettime(CLOCK_MONOTONIC, &s); \
        {   \

#define clock_end(s, e, t)  \
        }   \
        clock_gettime(CLOCK_MONOTONIC, &e); \
        t = (e.tv_sec - s.tv_sec) * 1e6 + (e.tv_nsec - s.tv_nsec) / 1e3;    \
    }   \

#define test_with_clock(s, e, t, code)  do{ \
    clock_gettime(CLOCK_MONOTONIC, &s); \
    { code }    \
    clock_gettime(CLOCK_MONOTONIC, &e); \
    t = (e.tv_sec - s.tv_sec) * 1e6 + (e.tv_nsec - s.tv_nsec) / 1e3;    \
}while(0);  \

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       test type list cost
 * 
 * @retval      test result
 */
int test_type_list_cost();

/**
 * @brief       check if type list correct
 * 
 * @retval      test result
 */
int test_type_list();

/**
 * @brief       test normal list cost
 * 
 * @retval      test result
 */
int test_normal_list_cost();

/**
 * @brief       test type hash
 * 
 * @retval      test result
 */
int test_type_hash();

/**
 * @brief       test memory pool
 * 
 * @retval      test result
 */
int test_mp();

/**
 * @brief       test non fixed mem pool
 * 
 * @note        测试非固定大小内存池
 */
int test_mp_nonfixed();

/**
 * @brief       测试非固定大小内存池性能
 */
int test_nonfixed_mp_cost();

/**
 * @brief       测试slab mp功能
 */
void test_slab_mp_function();

/**
 * @brief       测试slab mp性能
 */
void test_slab_mp_cost();

/**
 * @brief       test atom queue for spsc 
 */
int test_aq_spsc();

/**
 * @brief       测试普通队列+锁
 */
int test_normal_queue();

/**
 * @brief       测试mpmc无锁队列
 */
int test_aq_mpmc();

/**
 * @brief       测试mpsc无锁队列
 */
int test_aq_mpsc();

/**
 * @brief       测试事件驱动线程
 */
int test_ev_thd();

/**
 * @brief       测试锁
 */
int test_ev_lock();

/**
 * @brief       测试自旋锁
 */
int test_ev_spinlock();

/**
 * @brief       测试消息队列
 */
int test_msg_q();

/**
 * @brief       测试堆
 */
int test_heap();

/**
 * @brief       测试跳表正确性
 */
int test_skiplist();

/**
 * @brief       测试跳表性能
 */
int test_skiplist_cost();

/**
 * @brief       测试线程池
 */
int test_thp();

/**
 * @brief       测试定时器
 */
int test_ev_timer();

/**
 * @brief       测试高精度定时器
 */
int test_ev_high_res_timer();

#endif