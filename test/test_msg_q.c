/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    test_msg_q.c
 * @brief   测试消息队列
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-06
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-06 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#define _POSIX_C_SOURCE 199309L
#include <pthread.h>
#include "test.h"
#include "msg_q/msg_q.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define MSG_Q_SENDER            (4)
#define MSG_COUNT_PER_SENDER    (2048)
#define MSG_Q_ELEM_SIZE         (sizeof(int))
#define MSG_Q_CAPACITY          (MSG_Q_SENDER*MSG_COUNT_PER_SENDER)

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static unsigned long sender_sum = 0;
static unsigned long sum = 0;
declare_msg_q(test0, MSG_Q_CAPACITY, MSG_Q_ELEM_SIZE)

void* sender(void *args)
{
    int i = 0;
    for(i = 0; i < MSG_COUNT_PER_SENDER; ++ i)
    {
        msg_q_push(test0, &i, sizeof(int));
        ATOM_FETCH_ADD(&sender_sum, i, MORDER_RELEASE);
    }
}

void* receiver(void *agrs)
{
    int i = 0;
    for(i = 0; i < MSG_COUNT_PER_SENDER * MSG_Q_SENDER; ++ i)
    {
        int data;
        msg_q_pop(test0, sizeof(int), msg_q_wait_forever, &data);
        sum += data;
    }
}

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
pre_declare_list(test_normal)
typedef struct{
    int data;
    test_normal_list_item_t item;
}data_t;
declare_list(test_normal, normal, data_t, item)
test_normal_list_head_t head = {};
data_t datas[MSG_Q_SENDER][MSG_COUNT_PER_SENDER] = {};

void* normal_sender(void *args)
{
    int i = 0;
    int index = (int)args;
    for(i = 0; i < MSG_COUNT_PER_SENDER; ++ i)
    {
        pthread_mutex_lock(&mtx);
        normal_list_add_tail(&head, &datas[index][i]);
        pthread_mutex_unlock(&mtx);
        ATOM_FETCH_ADD(&sender_sum, i, MORDER_RELEASE);
    }

    return NULL;
}
void* normal_receiver(void *agrs)
{
    int i = 0;
    for(i = 0; i < MSG_COUNT_PER_SENDER * MSG_Q_SENDER;)
    {
        pthread_mutex_lock(&mtx);
        data_t *data = normal_list_pop(&head);
        pthread_mutex_unlock(&mtx);
        if(!data)
            continue;
        else
        {
            sum += data->data;
            ++ i;
        }
    }

    return NULL;
}


int test_msg_q()
{
    int i = 0;
    pthread_t s[MSG_Q_SENDER] = {}, r;
    struct timespec st,ed;
    unsigned long t;

    test_with_clock(
        st,ed,t,
        {
            pthread_create(&r, NULL, receiver, NULL);
            for(i = 0; i < MSG_Q_SENDER; ++ i)
                pthread_create(&s[i], NULL, sender, NULL);
            for(i = 0; i < MSG_Q_SENDER; ++ i)
                pthread_join(s[i], NULL);
            pthread_join(r, NULL);
        }
    );

    assert(sum == sender_sum);
    printf("msg_q: %d sender, 1 receiver, %d items cost time %d us, average %.3f us/item\n", MSG_Q_SENDER, 
        MSG_Q_SENDER * MSG_COUNT_PER_SENDER, t, (double)t/(MSG_Q_SENDER * MSG_COUNT_PER_SENDER));

    normal_list_init(&head);
    int j = 0;
    for(i = 0; i < MSG_Q_SENDER; ++ i)
        for(j = 0; j < MSG_COUNT_PER_SENDER; ++ j)
            datas[i][j].data = j;

    sender_sum = 0;
    sum = 0;
    test_with_clock(
        st,ed,t,
        {
            pthread_create(&r, NULL, normal_receiver, NULL);
            for(i = 0; i < MSG_Q_SENDER; ++ i)
                pthread_create(&s[i], NULL, normal_sender, (void*)i);
            for(i = 0; i < MSG_Q_SENDER; ++ i)
                pthread_join(s[i], NULL);
            pthread_join(r, NULL);
        }
    );

    assert(sum == sender_sum);
    printf("normal mutex queue: %d sender, 1 receiver, %d items cost time %d us, average %.3f us/item\n", MSG_Q_SENDER, 
        MSG_Q_SENDER * MSG_COUNT_PER_SENDER, t, (double)t/(MSG_Q_SENDER * MSG_COUNT_PER_SENDER));

    return 0;
}