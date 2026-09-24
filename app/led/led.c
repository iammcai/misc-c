/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    led.c
 * @brief   led模块实现
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

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <sys/ioctl.h>
#include "led/led.h"
#include "led/miscc_led_ioctl.h"
#include "plat/atom.h"
#include "cli/cli.h"
#include "syslog/syslog.h"
#include "web/web.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// led文件路径
#define LED_FILE_PATH       "/dev/miscc_led"

// 开启led对应的字符串
#define LED_ON_STR          "1"
// 关闭led对应的字符串
#define LED_OFF_STR         "0"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 全局led状态
static ATOMIC_UINT8_T g_led_status = LED_STATUS_OFF;

// 全局fd
static int g_led_fd = -1;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

/**
 * @brief       cli hook for led on
 * 
 * @note        turn led on
 */
void led_on_cli_hook(unsigned char argc, char *argv[])
{
    if(ATOM_LOAD(&g_led_status, MORDER_RELAXED) == LED_STATUS_ON)
        return;
    
    int on = 1;
    if(ioctl(g_led_fd, LED_IOCTL_SET_STATE, &on) < 0)
    {
        safe_printf("-led: ioctl() fail.\n");
        return;
    }

    ATOM_STORE(&g_led_status, LED_STATUS_ON, MORDER_RELAXED);
    syslog_notice(SYSLOG_MODULE_LED, "led turn on");
}

/**
 * @brief       cli hook for led off
 * 
 * @note        turn led off
 */
void led_off_cli_hook(unsigned char argc, char *argv[])
{
    if(ATOM_LOAD(&g_led_status, MORDER_RELAXED) == LED_STATUS_OFF)
        return;

    int off = 0;
    if(ioctl(g_led_fd, LED_IOCTL_SET_STATE, &off) < 0)
    {
        safe_printf("-led: ioctl() fail.\n");
        return;
    }
    ATOM_STORE(&g_led_status, LED_STATUS_OFF, MORDER_RELAXED);

    syslog_notice(SYSLOG_MODULE_LED, "led turn off");
}

error_code_e led_turn_http_cgi_cb(char *buf, size_t buf_size)
{
    led_status_e status = ATOM_LOAD(&g_led_status, MORDER_RELAXED);
    switch(status)
    {
        case LED_STATUS_OFF:
            led_on_cli_hook(0, NULL);
            snprintf(buf, buf_size, "%s", "{\"status\":\"on\"}");
            break;
        case LED_STATUS_ON:
            led_off_cli_hook(0, NULL);
            snprintf(buf, buf_size, "%s", "{\"status\":\"off\"}");
            break;
        case LED_STATUS_UNKNOWN:
            snprintf(buf, buf_size, "%s", "{\"status\":\"unknown\"}");
            break;
        default:
            return ERR_NOT_SUPPORT;
    }

    return ERR_NO_ERROR;
}

error_code_e led_status_http_cgi_cb(char *buf, size_t buf_size)
{
    led_status_e status = ATOM_LOAD(&g_led_status, MORDER_RELAXED);
    switch(status)
    {
        case LED_STATUS_OFF:
            snprintf(buf, buf_size, "%s", "{\"led_status\":\"off\"}");
            break;
        case LED_STATUS_ON:
            snprintf(buf, buf_size, "%s", "{\"led_status\":\"on\"}");
            break;
        case LED_STATUS_UNKNOWN:
            snprintf(buf, buf_size, "%s", "{\"led_status\":\"unknown\"}");
            break;
        default:
            return ERR_NOT_SUPPORT;
    }
    return ERR_NO_ERROR;
}

void led_module_init()
{
    // 加载驱动，忽略错误
    system("insmod ../driver/led/miscc_led.ko");

    g_led_status = LED_STATUS_UNKNOWN;

    // 打开led文件
    g_led_fd = open(LED_FILE_PATH, O_RDWR);
    if(g_led_fd < 0)
    {
        dbg_error("led module init fail");
        return;
    }

    // 读取led状态来设置初始值
    int status = 0;
    if(ioctl(g_led_fd, LED_IOCTL_GET_STATE, &status) < 0)
    {
        dbg_error("get led gpio status fail");
        dbg_error("led module init fail");
        return;
    }
    g_led_status = status == 0 ? LED_STATUS_OFF : LED_STATUS_ON;

    dbg_major("led module init ok");
}

void led_module_exit()
{
    // 退出时熄灭led
    led_off_cli_hook(0, NULL);

    // 关闭led文件
    if(g_led_fd >= 0)
        close(g_led_fd);

    // 卸载led驱动
    system("rmmod miscc_led");

    dbg_major("led module exit ok");
}