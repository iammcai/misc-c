/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    ev_lock.c
 * @brief   用户态锁实现
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

#include <stdlib.h>
#include <sched.h>
#include "event/ev_lock.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define EV_RW_LOCK_SPIN_CNT (10)

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

/**
 * @brief       asm yield
 * 
 * @note        不让出CPU，硬件流水线暂停一会
 */
static attr_force_inline void ev_pause(void)
{
	__asm__ __volatile__ ("yield");
}

void ev_rd_lock(ev_rwlock_t *lock)
{
    int retry = EV_RW_LOCK_SPIN_CNT;

    // 等待写者退出
    while(ATOM_LOAD(&lock->writing, MORDER_ACQUIRE))
    {
        if(retry--)
        {
            ev_pause();
            continue;
        }

        retry = EV_RW_LOCK_SPIN_CNT;
        sched_yield();      // 让出CPU，挂到就绪队列尾
    }

    ATOM_FETCH_ADD(&lock->readers, 1, MORDER_RELEASE);
}

void ev_wr_lock(ev_rwlock_t *lock)
{
    int retry = EV_RW_LOCK_SPIN_CNT;
    char expected = 0;

    // 先占有writing，防止新的读者进来
    while(!ATOM_CMP_XCHG_WEAK(&lock->writing, &expected, 1, MORDER_ACQ_REL, MORDER_RELAXED))
    {
        expected = 0;
        if(retry --)
        {
            ev_pause();
            continue;
        }

        retry = EV_RW_LOCK_SPIN_CNT;
        sched_yield();
    }

    // 等待所有读者退出
    retry = EV_RW_LOCK_SPIN_CNT;
    while(ATOM_LOAD(&lock->readers, MORDER_ACQUIRE))
    {
        if(retry --)
        {
            ev_pause();
            continue;
        }

        retry = EV_RW_LOCK_SPIN_CNT;
        sched_yield();
    }
}