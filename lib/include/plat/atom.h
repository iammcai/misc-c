/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    atom.h
 * @brief   原子操作头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-04-29
 * @version 1.0
 *
 * @note    提供原子操作，本文件使用GCC内置函数，可以在C89/C99使用
 *
 * @history
 *   1.0 | 2026-04-29 | cai | Initial creation.
 */

#ifndef __ATOMIC_H__
#define __ATOMIC_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

#define ATOM_PTR_NULL   (0)

// 指针和uint相互转换，C语言不支持对指针直接原子操作，需要转为uint
#define ATOM_PTR2UNIT(p)    ((uintptr_t)p)
#define ATOM_UINT2PTR(u)    ((void*)u)

// 六大内存序
#define MORDER_RELAXED      __ATOMIC_RELAXED
#define MORDER_CONSUME      __ATOMIC_CONSUME
#define MORDER_ACQUIRE      __ATOMIC_ACQUIRE
#define MORDER_RELEASE      __ATOMIC_RELEASE
#define MORDER_ACQ_REL      __ATOMIC_ACQ_REL
#define MORDER_SEQ_SCT      __ATOMIC_SEQ_CST

// 原子操作
#define ATOM_LOAD           __atomic_load_n
#define ATOM_STORE          __atomic_store_n
#define ATOM_XCHG           __atomic_exchange_n
#define ATOM_FETCH_ADD      __atomic_fetch_add
#define ATOM_FETCH_SUB      __atomic_fetch_sub
#define ATOM_ADD_FETCH      __atomic_add_fetch
#define ATOM_SUB_FETCH      __atomic_sub_fetch
#define ATOM_FETCH_AND      __atomic_fetch_and
#define ATOM_FETCH_OR       __atomic_fetch_or
#define ATOM_AND_FETCH      __atomic_and_fetch
#define ATOM_OR_FETCH       __atomic_or_fetch

// 原子CAS，a e 为指针。如果*a==*e，那么更新*a=d；否则*e=*a
#define ATOM_CMP_XCHG_WEAK(a, e, d, m1, m2) \
    __atomic_compare_exchange_n(a, e, d, 1, m1, m2)
/* ATOM_CMP_XCHG_WEAK end */
#define ATOM_CMP_XCHG_STRONG(a, e, d, m1, m2) \
    __atomic_compare_exchange_n(a, e, d, 0, m1, m2)
/* ATOM_CMP_XCHG_STRONG end*/

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 原子类型定义
typedef volatile char               ATOMIC_INT8_T;
typedef volatile unsigned char      ATOMIC_UINT8_T;
typedef volatile short              ATOMIC_INT16_T;
typedef volatile unsigned short     ATOMIC_UINT16_T;
typedef volatile int                ATOMIC_INT32_T;
typedef volatile unsigned int       ATOMIC_UINT32_T;
typedef volatile long long          ATOMIC_INT64_T;
typedef volatile unsigned long long ATOMIC_UINT64_T;
typedef volatile uintptr_t          ATOMIC_UINTPTR_T;
typedef volatile size_t             ATOMIC_SIZE_T;

#endif
/* __ATOMIC_H__ end */