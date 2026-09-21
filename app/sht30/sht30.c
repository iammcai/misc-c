/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    sht30.c
 * @brief   sht30用户态实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-31
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-31 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include "sht30/sht30.h"
#include "sht30/miscc_sht30.h"
#include "cli/cli.h"
#include "event/ev_thread.h"
#include "syslog/syslog.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// sysfs中hwmon设备目录前缀
#define HWMON_BASE_PATH             "/sys/class/hwmon"

// sysfs hwmon temp raw文件
#define SHT30_ATTR_TEMP_RAW         "temp1_input"
// sysfs hwmon humi raw文件
#define SHT30_ATTR_HUMI_RAW         "humidity1_input"

// 设备路径最大长度
#define FILE_PATH_MAX_LEN   (256)

// 120s检测一次
#define SHT30_MEASURE_INTERVAL      (120 * 1000)

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       find hwmon device path by name
 * 
 * @param[in]   name    - device name
 * 
 * @retval      error code
 */
static error_code_e _sht30_find_device_path_by_name(const char *name);

/**
 * @brief       read sht30 attr
 * 
 * @param[in]   attr_name   - attr name
 * 
 * @param[out]  value       - result
 * 
 * @retval      error code
 */
static error_code_e _sht30_read_attr(const char *attr_name, double *value);

static attr_force_inline void _sht30_ev_thd_measure(void *args)
{
    double temp_raw = 0.0, humi_raw = 0.0;

    if(ERR_NO_ERROR != _sht30_read_attr(SHT30_ATTR_HUMI_RAW, &humi_raw) ||
        ERR_NO_ERROR != _sht30_read_attr(SHT30_ATTR_TEMP_RAW, &temp_raw))
    {
        syslog_err(SYSLOG_MODULE_SHT30, "Periodic temperature and humidity monitoring failed");
        return;
    }

    char ctx[SYSLOG_ENTRY_CTX_SIZE] = {};
    snprintf(ctx, SYSLOG_ENTRY_CTX_SIZE, "Periodic monitoring: temperature %.3f ℃ humidity %.3f %%.", temp_raw/1000.0, humi_raw/1000.0);
    syslog_notice(SYSLOG_MODULE_SHT30, ctx);
}

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// sht30设备文件路径
static char g_sht30_file_path[FILE_PATH_MAX_LEN] = {};

// 事件线程，定时检测
declare_ev_thd(sht30_measure, _sht30_ev_thd_measure, NULL, SHT30_MEASURE_INTERVAL);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static error_code_e _sht30_find_device_path_by_name(const char *name)
{
    DIR *dir = NULL;
    const struct dirent *entry = NULL;
    char name_file[FILE_PATH_MAX_LEN] = {};
    char name_buf[FILE_PATH_MAX_LEN] = {};

    dir = opendir(HWMON_BASE_PATH);       // 打开hwmon设备目录
    if(!dir)
    {
        dbg_error("opendir() %s fail", HWMON_BASE_PATH);
        return ERR_SHT30_DEVICE_NOT_EXIST;
    }

    while(entry = readdir(dir))
    {
        if(0 != strncmp(entry->d_name, "hwmon", strlen("hwmon")))
            continue;

        memset(name_file, 0, FILE_PATH_MAX_LEN);
        snprintf(name_file, FILE_PATH_MAX_LEN, "%s/%s/name", HWMON_BASE_PATH, entry->d_name);

        int fd = open(name_file, O_RDONLY);
        if(fd < 0)
            continue;
        int n = read(fd, name_buf, sizeof(name_buf)-1);
        close(fd);
        if(n <= 0)
            continue;
        // read sysfs文件一般以换行符结尾
        name_buf[n] = '\0';
        char *newline = strchr(name_buf, '\n');
        if(newline)
            *newline = '\0';

        if(0 == strcmp(name_buf, name))
        {
            snprintf(g_sht30_file_path, FILE_PATH_MAX_LEN, "%s/%s", HWMON_BASE_PATH, entry->d_name);
            closedir(dir);
            return ERR_NO_ERROR;
        }
    }

    closedir(dir);
    dbg_error("hwmon device %s not found", name);
    return ERR_SHT30_DEVICE_NOT_EXIST;
}

static error_code_e _sht30_read_attr(const char *attr_name, double *value)
{
    if(!attr_name || !value)
        return ERR_BAD_PARAM;

    // 生成文件路径
    char filepath[FILE_PATH_MAX_LEN] = {};
    snprintf(filepath, FILE_PATH_MAX_LEN, "%s/%s", g_sht30_file_path, attr_name);

    int fd = open(filepath, O_RDONLY);
    if(fd < 0)
    {
        dbg_error("open() %s fail", filepath);
        return ERR_SHT30_ATTR_NOT_EXIST;
    }

    char buf[32] = {};      // 32足够
    int n = read(fd, buf, sizeof(buf)-1);
    close(fd);

    if(n <= 0)
    {
        dbg_error("read() %s fail", filepath);
        return ERR_SHT30_ATTR_NOT_EXIST;
    }

    buf[n] = '\0';
    *value = strtod(buf, NULL);     // string to double

    return ERR_NO_ERROR;
}

void sht30_show_temp(unsigned char argc, char *argv[])
{
    double temp_raw = 0.0;

    if(ERR_NO_ERROR != _sht30_read_attr(SHT30_ATTR_TEMP_RAW, &temp_raw))
    {
        safe_printf("-sht30: Measure temperature fail.\n");
        return;
    }

    safe_printf("-sht30: Temperature: %.3f ℃.\n", temp_raw / 1000.0);
}

void sht30_show_humi(unsigned char argc, char *argv[])
{
    double humi_raw = 0.0;

    if(ERR_NO_ERROR != _sht30_read_attr(SHT30_ATTR_HUMI_RAW, &humi_raw))
    {
        safe_printf("-sht30: Measure temperature fail.\n");
        return;
    }

    safe_printf("-sht30: Humidity: %.3f %%.\n", humi_raw / 1000.0);
}

error_code_e sht30_read_temperature(double *temp)
{
    double temp_raw = 0.0;

    pfm_ensure_ret(temp, ERR_BAD_PARAM);

    if(ERR_NO_ERROR != _sht30_read_attr(SHT30_ATTR_TEMP_RAW, &temp_raw))
    {
        return ERR_SHT30_HW_ERR;
    }

    *temp = temp_raw / 1000.0;

    return ERR_NO_ERROR;
}

error_code_e sht30_read_humidity(double *humi)
{
    double humi_raw = 0.0;

    pfm_ensure_ret(humi, ERR_BAD_PARAM);

    if(ERR_NO_ERROR != _sht30_read_attr(SHT30_ATTR_HUMI_RAW, &humi_raw))
    {
        return ERR_SHT30_HW_ERR;
    }

    *humi = humi_raw / 1000.0;

    return ERR_NO_ERROR;
}

error_code_e sht30_sensor_http_cgi_cb(char *buf, size_t buf_size)
{
     double temp_raw = 0.0;
     double humi_raw = 0.0;

    if(ERR_NO_ERROR != _sht30_read_attr(SHT30_ATTR_HUMI_RAW, &humi_raw) ||
        ERR_NO_ERROR != _sht30_read_attr(SHT30_ATTR_TEMP_RAW, &temp_raw))
    {
        return ERR_SHT30_HW_ERR;
    }

    snprintf(buf, buf_size, "{\"temperature\":%.1f,\"humidity\":%.1f}", temp_raw / 1000.0, humi_raw / 1000.0);
    return ERR_NO_ERROR;
}

void sht30_module_init()
{
    // 加载驱动，忽略错误
    system("insmod ../driver/sht30/miscc_sht30.ko");

    // 寻找设备节点
    if(ERR_NO_ERROR != _sht30_find_device_path_by_name(SHT30_HWMON_DEVICE_NAME))
    {
        dbg_error("sht30 device not exist, init fail");
        return;
    }

    // 注册定时任务检测温度湿度
    ev_thd_register(sht30_measure);
    ev_thd_run(sht30_measure);
    // 初始化时立即测量一次
    ev_thd_wake(sht30_measure);

    dbg_major("sht30 module init ok");
}

void sht30_module_deinit()
{
    // 停止定时检测事件线程
    ev_thd_cancel(sht30_measure);

    // 注销驱动
    system("rmmod miscc_sht30");

    dbg_major("sht30 module deinit ok");
}