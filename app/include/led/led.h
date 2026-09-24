/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    led.h
 * @brief   led模块头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-30
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-30 | cai | Initial creation.
 */

#ifndef __LED_H__
#define __LED_H__

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#include "plat/compiler.h"
#include "plat/debug.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// led状态枚举
typedef enum{
    LED_STATUS_OFF = 0,     // 灭
    LED_STATUS_ON = 1,      // 亮
    LED_STATUS_UNKNOWN = 2, // 状态未知
}led_status_e;

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       cli hook for led on
 * 
 * @note        turn led on
 */
extern void led_on_cli_hook(unsigned char argc, char *argv[]);

/**
 * @brief       cli hook for led off
 * 
 * @note        turn led off
 */
extern void led_off_cli_hook(unsigned char argc, char *argv[]);

/**
 * @brief       cb for http cgi: /api/led/turn
 * 
 * @retval      led status json
 */
extern error_code_e led_turn_http_cgi_cb(char *buf, size_t buf_size);

/**
 * @brief       cb for http cgi: /api/led/status
 * 
 * @retval      led status json
 */
extern error_code_e led_status_http_cgi_cb(char *buf, size_t buf_size);

/**
 * @brief       初始化led模块
 */
extern void led_module_init();

/**
 * @brief       led模块退出
 */
extern void led_module_exit();

#endif