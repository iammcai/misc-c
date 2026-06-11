/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    test_skiplist.c
 * @brief   测试跳表
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-11
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-11 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#define _POSIX_C_SOURCE 199309L
#include <stdlib.h>
#include "test.h"
#include "type/type_skiplist.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

pre_declare_skiplist(test)

typedef struct{
    int data;
    test_skiplist_item_t item;
}data_t;

static attr_pure_inline int data_cmp(const data_t *d1, const data_t *d2)
{
    return d1->data - d2->data;
}

declare_skiplist(test, test, data_t, item, data_cmp)

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define TEST_DATA_DOUNT     (20)

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

static test_skiplist_head_t sl_head = {};

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

/**
 * @brief       dump skiplist for debug
 */
static void data_skiplist_dump()
{
    int level = SKIPLIST_DEPTH_MAX-1;
    skiplist_item_t *it = NULL;
    
    printf("sl count: %d\n", sl_head.skiplist_head.count);
    printf("sl first %d, last %d\n", test_skiplist_first(&sl_head)->data, test_skiplist_last(&sl_head)->data);
    for(; level >= 0; -- level)
    {
        printf("level %d: ", level);
        it = sl_head.skiplist_head.head_item.next[level];
        while(it)
        {
            data_t *data = container_of(it, data_t, item.skiplist_item);
            printf("%d -> ", data->data);
            it = it->next[level];
        }
        printf("\n");
    }
}

int test_skiplist()
{
    data_t datas[TEST_DATA_DOUNT] = {};
    int i = 0;
    for(; i < TEST_DATA_DOUNT; ++ i)
    {
        datas[i].data = i;
        test_skiplist_add(&sl_head, &datas[i]);
    }

    data_skiplist_dump();

    for(i = TEST_DATA_DOUNT/3; i < TEST_DATA_DOUNT/2; ++ i)
        test_skiplist_del(&sl_head, &datas[i]);

    data_skiplist_dump();
    
    // test ceil
    data_t d1 = {.data = -10};
    data_t *d1_ceil = test_skiplist_ceil(&sl_head, &d1);
    data_t *d1_floor = test_skiplist_floor(&sl_head, &d1);
    printf("%d, ceil: %d, floor %d (-1 means not found)\n", d1.data,
        d1_ceil ? d1_ceil->data : -1,
        d1_floor ? d1_floor->data : -1
    );

    return 0;
}

/***************************************************************************************** */

#define TEST_COST_COUNT     (10000)
#define TEST_COST_TIMES     (1)

static test_skiplist_head_t sl_head_1 = {};

int test_skiplist_cost()
{
    data_t *p_data = calloc(TEST_COST_TIMES*TEST_COST_COUNT, sizeof(data_t));
    struct timespec s,e;
    unsigned long t;
    test_skiplist_init(&sl_head_1);

    int i = 0, j = 0;
    for(; i < TEST_COST_TIMES; ++ i)
    {
        test_with_clock(
            s,e,t,
            {
                for(j = 0; j < TEST_COST_COUNT; ++ j)
                {
                    p_data->data = i*TEST_COST_COUNT+j;
                    test_skiplist_add(&sl_head_1, &p_data[i*TEST_COST_COUNT+j]);
                }
            }
        );
        printf("no.%d add %d items cost %u us\n", i, TEST_COST_COUNT, t);
    }

    return 0;
}