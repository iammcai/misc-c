/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    heart_beat.h
 * @brief   进程心跳头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-24
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-09-24 | cai | Initial creation.
 */

#ifndef __HEART_BEAT_H__
#define __HEART_BEAT_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "plat/compiler.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       init heartbeat for process
 * 
 * @note        初始化心跳管理，core进程接收，其它进程发送
 */
extern void proc_heart_beat_module_init();

/**
 * @brief       process exit heartbeat
 * 
 * @note        结束heartbeat
 */
extern void proc_heart_beat_module_exit();

extern void proc_heart_beat_show_cli_cb(unsigned char argc, char *argv[]);

#endif