/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    ssd1306.h
 * @brief   ssd1306用户态实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-08-20
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-08-20 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

#include "ssd1306/ssd1306.h"
#include "event/ev_thread.h"
#include "sht30/sht30.h"
#include "event/ev_timer.h"
#include "sysmon/sysmon.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 定时刷新间隔，1s
#define SSD1306_FLUSH_INTERVAL      (1000)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       flush ssd1306, for ev thd work func
 * 
 * @note        ssd1306定时刷新函数
 */
static attr_force_inline void _ssd1306_flush(void *args);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// ssd1306设备文件fd
static int g_ssd1306_fd = -1;

// 事件线程，定时刷新屏幕内容
declare_ev_thd(ssd1306_flush, _ssd1306_flush, NULL, SSD1306_FLUSH_INTERVAL)

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static inline void _ssd1306_flush(void *args)
{
    char buf[SSD1306_BUF_SIZE] = {};
    double temp = 0.0;
    double humi = 0.0;
    double usage = 0.0;

    if(g_ssd1306_fd < 0)
    {
        dbg_error("device file not exist");
        return;
    }

    // 先清除屏幕
    snprintf(buf, sizeof(buf), "%s", SSD1306_CLEAR);
    write(g_ssd1306_fd, buf, strlen(buf));

    // 时间
    memset(buf, 0, sizeof(buf));
    if(ERR_NO_ERROR == realtime_get_str_brief(buf, SSD1306_BUF_SIZE))
        write(g_ssd1306_fd, buf, strlen(buf));
    else
        write(g_ssd1306_fd, "--:-- --:--:--", strlen("--:-- --:--:--"));

    // 温度
    memset(buf, 0, sizeof(buf));
    if(ERR_NO_ERROR == sht30_read_temperature(&temp))
        snprintf(buf, sizeof(buf), "%.3f degC", temp);
    else
        snprintf(buf, sizeof(buf), "--- degC");
    write(g_ssd1306_fd, buf, strlen(buf));

    // 湿度
    memset(buf, 0, sizeof(buf));
    if(ERR_NO_ERROR == sht30_read_humidity(&humi))
        snprintf(buf, sizeof(buf), "%.3f %%RH", humi);
    else
        snprintf(buf, sizeof(buf), "--- %%RH");
    write(g_ssd1306_fd, buf, strlen(buf));

    // cpu usage
    memset(buf, 0, sizeof(buf));
    sysmon_cpu_usage_get(&usage);
    snprintf(buf, sizeof(buf), "cpu %.2f%%", usage);
    write(g_ssd1306_fd, buf, strlen(buf));
}

void ssd1306_module_init()
{
    // 加载驱动，忽略报错
    system("insmod ../driver/ssd1306/miscc_ssd1306.ko");

    // 打开设备文件
    g_ssd1306_fd = open("/dev/miscc_ssd1306", O_WRONLY);
    if(g_ssd1306_fd < 0)
    {
        dbg_error("open ssd1306 file fail");
        return;
    }

    // 注册事件线程定时刷新
    ev_thd_register(ssd1306_flush)
    ev_thd_run(ssd1306_flush)

    dbg_major("ssd1306 module init ok");
}

void ssd1306_module_deinit()
{
    // 停止ev thd
    ev_thd_cancel(ssd1306_flush)

    // 关闭设备文件
    if(g_ssd1306_fd >= 0)
    {
        write(g_ssd1306_fd, SSD1306_CLEAR, strlen(SSD1306_CLEAR));
        write(g_ssd1306_fd, "sys exit, bye~", strlen("sys exit, bye~"));
        close(g_ssd1306_fd);
        sleep(2);
    }

    // 卸载驱动
    system("rmmod miscc_ssd1306");

    dbg_major("ssd1306 module deinit ok");
}