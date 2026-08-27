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

#include "plat/atom.h"
#include "ssd1306/ssd1306.h"
#include "sht30/sht30.h"
#include "event/ev_timer.h"
#include "sysmon/sysmon.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 定时刷新间隔，1s
#define SSD1306_FLUSH_INTERVAL      (1000)
// 页面更新间隔，5s
#define SSD1306_PAGE_INC_INTERVAL   (5000)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// ssd显示页面编号
typedef enum{
    SSD1306_PAGE_START = 0,

    SSD1306_PAGE_TIME,
    SSD1306_PAGE_SHT30,
    SSD1306_PAGE_SYSMON,

    SSD1306_PAGE_END,
}ssd1306_page_e;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       flush ssd1306
 * 
 * @note        ssd1306定时刷新屏幕函数
 */
static attr_force_inline void _ssd1306_flush(void *args);

/**
 * @brief       update ssd1306 page
 * 
 * @note        定时更新屏幕页面编号，以便展示不同信息
 */
static attr_force_inline void _ssd1306_page_inc(void *args);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// ssd1306设备文件fd
static int g_ssd1306_fd = -1;

// ssd1306当前页面编号
static ATOMIC_UINT8_T g_ssd1306_page = SSD1306_PAGE_TIME;

// 刷新屏幕内容定时器
static ev_timer_t *g_ssd1306_flush_timer = NULL;

// 刷新页面编号定时器
static ev_timer_t *g_ssd1306_page_update_timer = NULL;

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

    // 获取当前页面编号
    ssd1306_page_e page = ATOM_LOAD(&g_ssd1306_page, MORDER_RELAXED);

    switch(page)
    {
        case SSD1306_PAGE_TIME:                 // 时间
            memset(buf, 0, sizeof(buf));
            snprintf(buf, sizeof(buf), "==> Time");
            write(g_ssd1306_fd, buf, strlen(buf));

            memset(buf, 0, sizeof(buf));
            if(ERR_NO_ERROR == realtime_get_str_brief(buf, SSD1306_BUF_SIZE))
                write(g_ssd1306_fd, buf, strlen(buf));
            else
                write(g_ssd1306_fd, "--:-- --:--:--", strlen("--:-- --:--:--"));

            uint64_t running = running_time_get() / 1000;
            uint16_t hour = running / 3600;
            uint8_t min = (running % 3600) / 60;
            uint8_t sec = running % 60;
            memset(buf, 0, sizeof(buf));
            snprintf(buf, sizeof(buf), "%uh %um %us", hour, min, sec);
            write(g_ssd1306_fd, buf, strlen(buf));

            // 计算距离下班的时间
            int hh, mm, ss;
            realtime_get(&hh, &mm, &ss);
            int eod = 0;
            if(hh < 11 || (hh == 11 && mm < 55))
                eod = (11 - hh) * 60 + (50 - mm);
            else if(hh < 18)
                eod = (18 - hh - 1) * 60 + (60 - mm);
            else if(hh < 20)
                eod = (20 - hh - 1) * 60 + (60 - mm);
            memset(buf, 0, sizeof(buf));
            if(eod)
                snprintf(buf, sizeof(buf), "EOD left %d m", eod);
            else
                snprintf(buf, sizeof(buf), "EOD comes ^v^");
            write(g_ssd1306_fd, buf, strlen(buf));

            break;
        case SSD1306_PAGE_SHT30:                // 温湿度
            memset(buf, 0, sizeof(buf));
            snprintf(buf, sizeof(buf), "==> Env");
            write(g_ssd1306_fd, buf, strlen(buf));

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
            break;
        case SSD1306_PAGE_SYSMON:               // cpu usage
            memset(buf, 0, sizeof(buf));
            snprintf(buf, sizeof(buf), "==> Sys");
            write(g_ssd1306_fd, buf, strlen(buf));

            memset(buf, 0, sizeof(buf));
            sysmon_cpu_usage_get(&usage);
            snprintf(buf, sizeof(buf), "cpu %.2f%%", usage);
            write(g_ssd1306_fd, buf, strlen(buf));
            break;
        default:
            dbg_error("ssd1306 page error");
            break;
    }

    // 继续定时器
    ev_timer_start(g_ssd1306_flush_timer);
}

static inline void _ssd1306_page_inc(void *args)
{
    ssd1306_page_e current = ATOM_LOAD(&g_ssd1306_page, MORDER_RELAXED);
    ssd1306_page_e next;

    if(g_ssd1306_page == SSD1306_PAGE_END - 1)
        next = SSD1306_PAGE_START + 1;
    else
        next = current + 1;

    ATOM_STORE(&g_ssd1306_page, next, MORDER_RELAXED);

    // 继续定时器
    ev_timer_start(g_ssd1306_page_update_timer);
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

    // 启动定时器刷新屏幕以及页面
    g_ssd1306_flush_timer = ev_timer_create("ssd1306_flush", SSD1306_FLUSH_INTERVAL, _ssd1306_flush, NULL);
    ev_timer_start(g_ssd1306_flush_timer);
    g_ssd1306_page_update_timer = ev_timer_create("ssd1306_page_update", SSD1306_PAGE_INC_INTERVAL, _ssd1306_page_inc, NULL);
    ev_timer_start(g_ssd1306_page_update_timer);

    dbg_major("ssd1306 module init ok");
}

void ssd1306_module_deinit()
{
    // 销毁定时器
    if(g_ssd1306_flush_timer)
        ev_timer_destroy(&g_ssd1306_flush_timer);

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