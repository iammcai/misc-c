/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    proc.h
 * @brief   进程相关通用头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-22
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-09-22 | cai | Initial creation.
 */

#ifndef __PROC_H__
#define __PROC_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 进程ID枚举
typedef enum{
    PID_CORE = 0,           // core进程

    PID_CNT,                // 统计数量用
}proc_id_e;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       set current process id
 * 
 * @param[in]   pid         - pid, alloc static
 */
extern void proc_id_set(proc_id_e pid);

/**
 * @brief       get current process id
 * 
 * @retval      proc_id
 */
extern proc_id_e proc_id_get();

/**
 * @brief       check if current proc is core process
 * 
 * @retval      0 - no, 1 - yes
 * 
 * @note`       判断当前进程是否core进程
 */
extern int proc_current_is_core();

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

#endif
/* __PROC_H__ end */