/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    test_hash.c
 * @brief   侵入式哈希表测试
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-05-27
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-05-27 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "type/type_hash.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define DATA_NAME_LEN       (32)
#define TEST_DATA_COUNT     (50000)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

pre_declare_hash(test)

// 测试数据结定义
typedef struct{
    char name[DATA_NAME_LEN];
    test_hash_item_t item;
}data_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

static attr_pure_inline int data_cmp(data_t *d1, data_t *d2)
{
    return strncmp(d1->name, d2->name, DATA_NAME_LEN);
}
static attr_pure_inline unsigned int data_hash(data_t *d)
{
    return type_hash_jhash(d->name, DATA_NAME_LEN, 0);
}

declare_hash(test, test, data_t, item, 1, 31, data_cmp, data_hash)

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

int test_type_hash()
{
    test_hash_head_t head = {};
    data_t data[TEST_DATA_COUNT] = {};
    int i = 0;
    struct timespec s, e;
    unsigned int add_time, find_time, del_time;

    // 准备数据
    for(i = 0; i < TEST_DATA_COUNT; ++ i)
        snprintf(data[i].name, DATA_NAME_LEN-1, "data_name_%d", i);

    test_hash_init(&head);

    // 插入若干元素
    clock_start(s, e, add_time)
    for(i = 0; i < TEST_DATA_COUNT; ++ i)
        assert(!test_hash_add(&head, &data[i]));
    clock_end(s, e, add_time)
    assert(TEST_DATA_COUNT == test_hash_count(&head));

    // 重复插入
    for(i = 0; i < TEST_DATA_COUNT; ++ i)
        assert(test_hash_add(&head, &data[i]));
    assert(TEST_DATA_COUNT == test_hash_count(&head));

    // 挨个查找
    clock_start(s, e, find_time)
    for(i = 0; i < TEST_DATA_COUNT; ++ i)
    {
        data_t temp;
        strncpy(temp.name, data[i].name, DATA_NAME_LEN);
        assert(&data[i] == test_hash_find(&head, &temp));
    }
    clock_end(s, e, find_time)

    // 删除
    clock_start(s, e, del_time)
    for(i = 0; i < TEST_DATA_COUNT; ++ i)
        assert(&data[i] == test_hash_del(&head, &data[i]));
    clock_end(s, e, del_time)
    assert(!test_hash_count(&head));

    printf("test type hash result: PASS\n");
    printf("add %d item into hash cost: %u us, %.3f us/i\n", TEST_DATA_COUNT, add_time, (double)add_time/TEST_DATA_COUNT);
    printf("find %d item in hash cost: %u us, %.3f us/i\n", TEST_DATA_COUNT, find_time, (double)find_time/TEST_DATA_COUNT);
    printf("del %d item in hash cost: %u us, %.3f us/i\n", TEST_DATA_COUNT, del_time, (double)del_time/TEST_DATA_COUNT);
    printf("========\n");

    return 0;
}