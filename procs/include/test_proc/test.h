/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    test.h
 * @brief   测试多进程头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-28
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-09-28 | cai | Initial creation.
 */

#ifndef __TEST_PROC_H__
#define __TEST_PROC_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "plat/compiler.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// shm name
#define TEST_PROCESS_RUNNING_SHM_NAME   "/test_proc_running_shm"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       test process entry
 */
extern void test_proc_entry();

#endif
/* __TEST_PROC_H__ end */