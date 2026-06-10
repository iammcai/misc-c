/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    test_heap.c
 * @brief   测试堆
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
#include "test.h"
#include "type/type_heap.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

pre_declare_heap(test)

typedef struct{
    int i;
    test_heap_item_t item;
}data_t;

static attr_pure_inline int data_cmp(const data_t *d1, const data_t *d2)
{
    return d1->i - d2->i;
}

declare_heap(test, test, data_t, item, 100, 0, data_cmp)

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define TEST_HEAP_ITEM_COUNT    (5000)

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

static test_heap_head_t heap = {};
static data_t data[TEST_HEAP_ITEM_COUNT] = {};

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

int test_heap()
{
    test_heap_init(&heap);

    int i = 0;
    for(i = TEST_HEAP_ITEM_COUNT-1; i >= 0; i -= 2)
    {
        data[i].i = i;
        test_heap_add(&heap, &data[i]);
    }
    for(i = TEST_HEAP_ITEM_COUNT-2; i >= 0; i -= 2)
    {
        data[i].i = i;
        test_heap_add(&heap, &data[i]);
    }

    for(i = TEST_HEAP_ITEM_COUNT/3; i < TEST_HEAP_ITEM_COUNT/2; ++ i)
    {
        test_heap_del(&heap, &data[i]);
    }

    data_t *d = test_heap_pop(&heap);
    int temp = d->i;
    while(d = test_heap_pop(&heap))
    {
        assert(d->i > temp);
        temp = d->i;
    }

    dbg_always("Test heap PASS");

    return 0;
}