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
#include "plat/compiler.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 信号量定义
typedef struct{
    sem_t sem;  // posix信号量
}ev_sem_t;

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