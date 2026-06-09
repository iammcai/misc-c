/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    ev_lock.h
 * @brief   用户态锁头文件
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

#ifndef __EV_LOCK_H__
#define __EV_LOCK_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <sched.h>
#include <pthread.h>
#include "plat/compiler.h"
#include "plat/atom.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 自旋锁定义
typedef struct{
    ATOMIC_UINT8_T flag;
}ev_spinlock_t;

// 读写锁定义
typedef struct{
    ATOMIC_UINT32_T readers;    // 读者数量
    ATOMIC_UINT8_T writing;     // 写者持有
}ev_rwlock_t;

// 互斥锁定义
typedef struct{
    pthread_mutex_t mutex;
}ev_mutex_t;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 最大自旋次数，超出则yield
#define EV_SPIN_LOCK_SPIN_COUNT (10)

// 静态初始化spinlock
#define EV_SPINLOCK_INITIALIZER { .flag = 0 }

// 动态初始化spinlock
#define ev_spinlock_init(lock)  \
    _ev_spinlock_init(lock);    \
/* ev_spinlock_init end */

/**
 * 外部使用，后接{}，代码块内处理受自旋锁保护，异常退出可自动解锁
 */
#define ev_with_spinlock(lock)  \
    for(ev_spinlock_t *_lock attr_cleanup(_ev_spin_unlock) = _ev_spin_lock(lock), *_once = NULL; _once == NULL; _once = (void*)1)   \
/* ev_with_spinlock end */

// 静态初始化rwlock
#define EV_RWLOCK_INITIALIZER   { .readers = 0, .writing = 0, }

// 动态初始化rwlock
#define ev_rwlock_init(lock)    \
    _ev_rdlock_init(lock);  \
/* ev_rwlock_init end */

/**
 * 外部使用，后接{}，代码块内的处理由读锁保护，异常退出或者正常退出均可安全释放
 */
#define ev_with_rdlock(lock)    \
    for(ev_rwlock_t *_lock attr_cleanup(_ev_rd_unlock) = _ev_rd_lock(lock), *_once = NULL; NULL == _once; _once = (void*)1) \
/* ev_with_rdlock end */

/**
 * 外部使用，后接{}，代码块内的处理由写锁保护，异常退出或者正常退出均可安全释放
 */
#define ev_with_wrlock(lock)    \
    for(ev_rwlock_t *_lock attr_cleanup(_ev_wr_unlock) = _ev_wr_lock(lock), *_once = NULL; NULL == _once; _once = (void*)1) \
/* ev_with_wrlock end */

// 静态初始化互斥锁
#define EV_MUTEX_INITIALIZER    { .mutex = PTHREAD_MUTEX_INITIALIZER, }

// 动态初始化互斥锁
#define ev_mutex_init(mutex)    \
    _ev_mutex_init(mutex);  \
/* ev_mutex_init end */

/**
 * 外部使用，后接{}，代码块内的处理由互斥锁保护，异常退出或者正常退出均可释放
 */
#define ev_with_mutex(mutex)    \
    for(ev_mutex_t *_mutex attr_cleanup(_ev_mutex_unlock) = _ev_mutex_lock(mutex), *_once = NULL; NULL == _once; _once = (void*)1)  \
/* ev_with_mutex end */

/* ========================================================================== */
/*                           Function Prototypes                              */
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

/**
 * @brief       init spinlock
 * 
 * @param[in]   lock    - spin lock
 */
static attr_force_inline void _ev_spinlock_init(ev_spinlock_t *lock)
{
    ATOM_STORE(&lock->flag, 0, MORDER_RELEASE);
}

/**
 * @brief       lock spin lock
 * 
 * @param[in]   lock    - ptr to spin lock
 * 
 * @retval      origin lock
 * 
 * @note        内部使用，上锁，返回锁
 */
static attr_force_inline ev_spinlock_t* _ev_spin_lock(ev_spinlock_t *lock)
{
    int flag = 0;
    int spin = EV_SPIN_LOCK_SPIN_COUNT;
    while(!ATOM_CMP_XCHG_WEAK(&lock->flag, &flag, 1, MORDER_SEQ_SCT, MORDER_RELAXED))   // 0->1，失败重试
    {
        flag = 0;
        if(--spin)
        {
            ev_pause();
            continue;
        }
        spin = EV_SPIN_LOCK_SPIN_COUNT;
        sched_yield();
    }

    return lock;
}

/**
 * @brief       unlock spin lock
 * 
 * @param[in]   lock    - 2nd ptr to spin lock
 * 
 * @note        内部使用，作为析构函数，解锁
 */
static attr_force_inline void _ev_spin_unlock(ev_spinlock_t **lock)
{
    ATOM_STORE(&(*lock)->flag, 0, MORDER_RELEASE);
}

/**
 * @brief       init rwlock
 * 
 * @param[in]   lock    - rw lock
 */
static attr_force_inline void _ev_rdlock_init(ev_rwlock_t *lock)
{
    ATOM_STORE(&lock->readers, 0, MORDER_RELEASE);
    ATOM_STORE(&lock->writing, 0, MORDER_RELEASE);
}

/**
 * @brief       lock read lock
 * 
 * @param[in]   lock    - rw lock
 * 
 * @note        等待读者退出，才能占读锁
 */
extern void ev_rd_lock(ev_rwlock_t *lock);

/**
 * @brief       unlock read lock
 * 
 * @param[in]   lock    - rw lock
 * 
 * @note        读者-1
 */
static attr_force_inline void ev_rd_unlock(ev_rwlock_t *lock)
{
    ATOM_FETCH_SUB(&lock->readers, 1, MORDER_RELEASE);
}

/**
 * @brief       lock read lock
 * 
 * @param[in]   lock    - ptr to lock
 * 
 * @retval      ptr to lock
 * 
 * @note        内部使用，返回lock
 */
static attr_force_inline ev_rwlock_t* _ev_rd_lock(ev_rwlock_t *lock)
{
    ev_rd_lock(lock);
    return lock;
}

/**
 * @brief       unlock read lock
 * 
 * @param[in]   lock    - ptr to ptr to lock
 * 
 * @note        内部使用，给cleanup指定，入参是二级指针
 */
static attr_force_inline void _ev_rd_unlock(ev_rwlock_t **lock)
{
    ev_rd_unlock(*lock);
}

/**
 * @brief       lock write lock
 * 
 * @param[in]   lock    - ptr to lock
 * 
 * @note        读者全部退出，才可以占有写锁
 */
extern void ev_wr_lock(ev_rwlock_t *lock);

/**
 * @brief       unlock write lock
 * 
 * @param[in]   lock    - ptr to ptr to lock
 * 
 * @note        将writing标志写0
 */
static attr_force_inline void ev_wr_unlock(ev_rwlock_t *lock)
{
    ATOM_STORE(&lock->writing, 0, MORDER_RELEASE);
}

/**
 * @brief       lock write lock
 * 
 * @param[in]   lock    - ptr to lock
 * 
 * @retval      ptr to lock
 * 
 * @note        内部使用，返回lock
 */
static attr_force_inline ev_rwlock_t* _ev_wr_lock(ev_rwlock_t *lock)
{
    ev_wr_lock(lock);
    return lock;
}

/**
 * @brief       unlock write lock
 * 
 * @param[in]   lock    - ptr to ptr to lock
 * 
 * @note        内部使用，给cleanup指定，入参是二级指针
 */
static attr_force_inline void _ev_wr_unlock(ev_rwlock_t **lock)
{
    ev_wr_unlock(*lock);
}

/**
 * @brief       init mutex
 * 
 * @param[in]   mutex   - ev mutex
 * 
 * @note        内部使用，初始化互斥锁
 */
static attr_force_inline void _ev_mutex_init(ev_mutex_t *mutex)
{
    pthread_mutex_init(&mutex->mutex, NULL);
}

/**
 * @brief       lock mutex
 * 
 * @param[in]   mutex   - ev mutex
 * 
 * @note        内部使用，互斥锁上锁，返回锁
 */
static attr_force_inline ev_mutex_t* _ev_mutex_lock(ev_mutex_t *mutex)
{
    pthread_mutex_lock(&mutex->mutex);
    return mutex;
}

/**
 * @brief       unlock mutex
 * 
 * @param[in]   mutex   - 2nd ptr to ev mutex
 * 
 * @note        内部使用，析构解锁
 */
static attr_force_inline void _ev_mutex_unlock(ev_mutex_t **mutex)
{
    pthread_mutex_unlock(&(*mutex)->mutex);
}

#endif