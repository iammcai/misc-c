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

#include "plat/compiler.h"
#include "plat/atom.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 读写锁定义
typedef struct{
    ATOMIC_UINT32_T readers;    // 读者数量
    ATOMIC_UINT8_T writing;     // 写者持有
}ev_rwlock_t;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

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

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

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

#endif