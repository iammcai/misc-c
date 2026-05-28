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
#include "type/type_list.h"

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
 * @brief       test memory pool cost
 * 
 * @retval      test result
 */
int test_mp_cost();

/**
 * @brief       test calloc free cost
 * 
 * @retval      test result
 */
int test_normal_mem_cost();

/**
 * @brief       test mp used in multi threads
 */
int test_mp_multi_thread();

#endif