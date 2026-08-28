/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    ev_sem.h
 * @brief   posix信号量头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-05
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-05 | cai | Initial creation.
 */

#ifndef __EV_SEM_H__
#define __EV_SEM_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <errno.h>
#include <semaphore.h>
#include <stdint.h>

#include "plat/compiler.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 信号量定义
typedef struct{
    sem_t sem;  // posix信号量
}ev_sem_t;

// 超时等待信号量返回值枚举
typedef enum{
    EV_SEM_WAKE = 0,        // 被信号量唤醒
    EV_SEM_TIMEOUT,         // 超时退出
}ev_sem_ret_e;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/**
 * @brief       init sem
 * 
 * @param[in]   sem - ptr to ev sem
 */
static attr_force_inline void ev_sem_init(ev_sem_t *sem)
{
    sem_init(&sem->sem, 0, 0);
}

/**
 * @brief       sem wait
 * 
 * @param[in]   sem - ptr to ev sem
 */
static attr_force_inline void ev_sem_wait(ev_sem_t *sem)
{
    while(sem_wait(&sem->sem) && EINTR == errno);
}

/**
 * @brief       wait sem, with timeout exit
 * 
 * @param[in]   sem
 * @param[in]   ms  - timeout, unit ms
 * 
 * @retval      exit method, post or timeout
 */
static attr_force_inline ev_sem_ret_e ev_sem_wait_ms(ev_sem_t *sem, uint32_t ms)
{
    int ret;
    struct timespec ts = {};    // sem_timedwait必须使用绝对超时时间，且类型realtime

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (ms / 1000);
    ts.tv_nsec += ((ms % 1000) * 1000000L);
    if (ts.tv_nsec >= 1000000000L)
    {
        ts.tv_sec ++;
        ts.tv_nsec -= 1000000000L;
    }

    do{
        ret = sem_timedwait(&sem->sem, &ts);
    }while(ret == -1 && errno == EINTR);

    if(ret == 0)                // 被唤醒
        return EV_SEM_WAKE;

    return EV_SEM_TIMEOUT;  // 超时退出
}

/**
 * @brief       sem post
 * 
 * @param[in]   sem - ptr to ev sem
 */
static attr_force_inline void ev_sem_post(ev_sem_t *sem)
{
    sem_post(&sem->sem);
}

/**
 * @brief       sem fini
 * 
 * @param[in]   sem - ptr to ev sem
 */
static attr_force_inline void ev_sem_fini(ev_sem_t *sem)
{
    sem_destroy(&sem->sem);
}

#endif
/* __EV_SEM_H__ end */