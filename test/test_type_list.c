/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    test_type_list.h
 * @brief   测试侵入式链表
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-05-26
 * @version 1.0
 *
 * @note    提供调试工具
 *
 * @history
 *   1.0 | 2026-05-26 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include "test.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define Malloc(s)   \
{(  \
    mem_alloc += s, \
    malloc(s)   \
)}  \

#define Free(p,s)   \
do{  \
    mem_free += s;  \
    free(p);    \
}while(0);  \

#define TEST_DATA_COUNT     (100000)

pre_declare_list(test)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

typedef struct{
    unsigned int data;
    test_list_item_t item;
}data_t;

declare_list(test, test, data_t, item)

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

static unsigned int mem_alloc = 0;
static unsigned int mem_free = 0;

/* ========================================================================== */
/*                           Normal list oper                                 */
/* ========================================================================== */

// 链表节点定义
typedef struct normal_list_item_s{
    int *data;
    struct normal_list_item_s *next;
}normal_list_item_t;

// 链表头定义
typedef struct{
    normal_list_item_t *first;
    normal_list_item_t **last_next;
    unsigned int count;
}normal_list_head_t;

static normal_list_item_t normal_list_end = {.data = 0, .next = NULL,};

// init
static attr_force_inline void normal_list_init(normal_list_head_t *head)
{
    assert(head);
    head->count = 0;
    head->first = &normal_list_end;
    head->last_next = &(normal_list_end.next);
}
// Fini
static attr_force_inline void normal_list_fini(normal_list_head_t *head)
{
    assert(head);
    memset(head, 0, sizeof(*head));
}
// add
static attr_force_inline void normal_list_add_head(normal_list_head_t *head, int *data)
{
    assert(head && data);
    normal_list_item_t *item = Malloc(sizeof(normal_list_item_t));
    assert(item);
    item->data = data;

    item->next = head->first;
    if(head->last_next == &(normal_list_end.next))
        head->last_next = &item->next;
    head->first = item;
    head->count ++;
}
static attr_force_inline void normal_list_add_tail(normal_list_head_t *head, int *data)
{
    assert(head && data);
    normal_list_item_t *item = Malloc(sizeof(normal_list_item_t));
    assert(item);
    item->data = data;

    item->next = NULL;
    if(head->first == &normal_list_end)
        head->first = item;
    *head->last_next = item;
    head->last_next = &item->next;

    head->count ++;
}
// del
static int* normal_list_pop(normal_list_head_t *head)
{
    int *ret = NULL;
    normal_list_item_t *first = NULL;

    assert(head);
    if(head->count == 0)
        return NULL;

    first = head->first;
    head->first = first->next;

    ret = first->data;
    Free(first, sizeof(normal_list_item_t));

    -- head->count;
    if(0 == head->count)
        normal_list_init(head);
}
// pop
// first
// next
// count

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

int test_normal_list_cost()
{
    mem_alloc = 0;
    mem_free = 0;

    int ret = 0;
    int *data = Malloc(sizeof(int)*TEST_DATA_COUNT);
    normal_list_head_t *head = Malloc(sizeof(normal_list_head_t));
    int i = 0;
    struct timespec start, end;
    unsigned int add_time = 0;
    unsigned int del_time = 0;

    memset(head, 0, sizeof(*head));
    memset(data, 0, sizeof(int)*TEST_DATA_COUNT);

    normal_list_init(head);

    clock_start(start, end, add_time)

    for(i = 0; i < TEST_DATA_COUNT/2; ++ i)
    {
        normal_list_add_head(head, &data[i]);
    }
    for(; i < TEST_DATA_COUNT; ++ i)
    {
        normal_list_add_tail(head, &data[i]);
    }

    clock_end(start, end, add_time)

    clock_start(start, end, del_time)

    for(i = 0; i < TEST_DATA_COUNT; ++ i)
    {
        normal_list_pop(head);
    }

    clock_end(start, end, del_time)

    Free(head, sizeof(*head));
    Free(data, sizeof(int)*TEST_DATA_COUNT);

    printf("test normal list cost: %s\n", ret == 0 ? "PASS" : "FAIL");
    printf("add %u item into normal list cost: %u us, %.3f i/us\n", TEST_DATA_COUNT, add_time, (double)add_time/TEST_DATA_COUNT);
    printf("pop %u item from normal list cost: %u us, %.3f i/us\n", TEST_DATA_COUNT, del_time, (double)del_time/TEST_DATA_COUNT);
    printf("alloc mem: %u Bytes\n", mem_alloc);
    printf("free mem: %u Bytes\n", mem_free);
    printf("========\n");

    return ret;
}

int test_type_list()
{
    test_list_head_t head = {};
    data_t data[TEST_DATA_COUNT] = {};
    int i = 0;

    test_list_init(&head);

    for(i = 0; i < TEST_DATA_COUNT; ++ i)
        data[i].data = i;

    test_list_add_head(&head, &data[0]);
    test_list_add_head(&head, &data[1]);
    test_list_add_head(&head, &data[2]);
    test_list_add_tail(&head, &data[3]);
    test_list_add_tail(&head, &data[4]);
    test_list_add_tail(&head, &data[5]);
    // 2->1->0->3->4->5
    assert(&data[2] == test_list_first(&head));
    assert(6 == test_list_count(&head));
    assert(&data[1] == test_list_next(&data[2]));
    assert(&data[0] == test_list_next(&data[1]));
    assert(&data[3] == test_list_next(&data[0]));
    assert(&data[4] == test_list_next(&data[3]));
    assert(&data[5] == test_list_next(&data[4]));

    test_list_del(&head, &data[2]);
    test_list_del(&head, &data[5]);
    test_list_del(&head, &data[0]);
    // 1->3->4
    assert(3 == test_list_count(&head));
    assert(&data[1] == test_list_first(&head));
    assert(&data[3] == test_list_next(&data[1]));
    assert(&data[4] == test_list_next(&data[3]));

    assert(&data[1] == test_list_pop(&head));
    assert(&data[3] == test_list_pop(&head));
    assert(&data[4] == test_list_pop(&head));

    assert(0 == test_list_count(&head));

    for(i = 0; i < TEST_DATA_COUNT; ++ i)
        test_list_add_tail(&head, &data[i]);

    for(i = 0; i < TEST_DATA_COUNT; ++ i)
        assert(i == test_list_pop(&head)->data);

    for(i = 0; i < TEST_DATA_COUNT; ++ i)
        test_list_add_head(&head, &data[i]);

    for(i = 0; i < TEST_DATA_COUNT; ++ i)
        assert((TEST_DATA_COUNT - i - 1) == test_list_pop(&head)->data);

    printf("test type list result: PASS\n");
    printf("========\n");
    return 0;
}

int test_type_list_cost()
{
    mem_alloc = 0;
    mem_free = 0;

    int ret = 0;
    test_list_head_t *list_head = Malloc(sizeof(test_list_head_t));
    data_t *data = Malloc(TEST_DATA_COUNT * sizeof(data_t));
    int i = 0;
    struct timespec start, end;
    unsigned int add_time = 0;
    unsigned int del_time = 0;

    memset(list_head, 0, sizeof(*list_head));
    memset(data, 0, TEST_DATA_COUNT * sizeof(data_t));

    test_list_init(list_head);

    clock_start(start, end, add_time)

    for(i = 0; i < TEST_DATA_COUNT/2; ++ i)
    {
        test_list_add_head(list_head, &data[i]);
    }
    for(; i < TEST_DATA_COUNT; ++ i)
    {
        test_list_add_tail(list_head, &data[i]);
    }

    clock_end(start, end, add_time)

    clock_start(start, end, del_time)

    for(i = 0; i < TEST_DATA_COUNT; ++ i)
    {
        test_list_pop(list_head);
    }

    clock_end(start, end, del_time)

    Free(data, TEST_DATA_COUNT * sizeof(data_t));
    Free(list_head, sizeof(*list_head));

    printf("test type list cost: %s\n", ret == 0 ? "PASS" : "FAIL");
    printf("add %u item into list cost: %u us, %.3f i/us\n", TEST_DATA_COUNT, add_time, (double)add_time/TEST_DATA_COUNT);
    printf("pop %u item from list cost: %u us, %.3f i/us\n", TEST_DATA_COUNT, del_time, (double)del_time/TEST_DATA_COUNT);
    printf("alloc mem: %u Bytes\n", mem_alloc);
    printf("free mem: %u Bytes\n", mem_free);
    printf("========\n");

    return ret;
}