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
#include "event/ev_thread.h"
#include "syslog/syslog.h"
#include "fifo/fifo.h"
#include "event/ev_lock.h"

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

// 60s检测一次
#define SHT30_MEASURE_INTERVAL      (60 * 1000)

// sht30记录历史数据个数
#define SHT30_HISTORY_SIZE          (128)
// 打印sht30历史记录的缓冲长度
#define SHT30_HISTORY_BUF_SIZE      (1024)

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

/**
 * @brief       update sht30 measure history
 * 
 * @param[in]   temp    - 实际温度
 * @param[out]  humi    - 实际湿度
 */
static void _sht30_history_update(double temp, double humi);

/**
 * @brief       ev thd work func for sht30
 */
static attr_force_inline void _sht30_ev_thd_measure(void *args)
{
    double temp_raw = 0.0, humi_raw = 0.0;

    if(ERR_NO_ERROR != _sht30_read_attr(SHT30_ATTR_HUMI_RAW, &humi_raw) ||
        ERR_NO_ERROR != _sht30_read_attr(SHT30_ATTR_TEMP_RAW, &temp_raw))
    {
        syslog_err(SYSLOG_MODULE_SHT30, "Periodic temperature and humidity monitoring failed");
        return;
    }

    _sht30_history_update(temp_raw/1000.0, humi_raw/1000.0);
}

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// sht30设备文件路径
static char g_sht30_file_path[FILE_PATH_MAX_LEN] = {};

// 事件线程，定时检测
declare_ev_thd(sht30_measure, _sht30_ev_thd_measure, NULL, SHT30_MEASURE_INTERVAL);

// 温度历史数据
static fifo_t *g_sht30_history_temp = NULL;
// 湿度历史数据
static fifo_t *g_sht30_history_humi = NULL;
// lock for history
static ev_spinlock_t g_sht30_history_splock = EV_SPINLOCK_INITIALIZER;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static void _sht30_history_update(double temp, double humi)
{
    double tmp;

    if(likely(g_sht30_history_temp))
        ev_with_spinlock(&g_sht30_history_splock)
        {
            if(fifo_is_full(g_sht30_history_temp))      // fifo满，则读掉第一个
                fifo_read(g_sht30_history_temp, (char*)&tmp, sizeof(double), 1);
            fifo_write(g_sht30_history_temp, &temp, sizeof(double));
        }

    if(likely(g_sht30_history_humi))
        ev_with_spinlock(&g_sht30_history_splock)
        {
            if(fifo_is_full(g_sht30_history_humi))
                fifo_read(g_sht30_history_humi, (char*)&tmp, sizeof(double), 1);
            fifo_write(g_sht30_history_humi, &humi, sizeof(double));
        }
}

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

    // ssd1306 spi盲发，会调用sht30获取数据，此处添加sht30路径判断，静默返回设备未存在错误
    if(g_sht30_file_path[0] == '\0')
        return ERR_NO_DEV;

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

void sht30_show_temp(cli_ctx_t *ctx, unsigned char argc, char *argv[])
{
    double temp_raw = 0.0;

    if(ERR_NO_ERROR != _sht30_read_attr(SHT30_ATTR_TEMP_RAW, &temp_raw))
    {
        cli_printf(ctx, "-sht30: Measure temperature fail.\n");
        return;
    }

    cli_printf(ctx, "-sht30: Temperature: %.3f ℃.\n", temp_raw / 1000.0);
}

void sht30_show_humi(cli_ctx_t *ctx, unsigned char argc, char *argv[])
{
    double humi_raw = 0.0;

    if(ERR_NO_ERROR != _sht30_read_attr(SHT30_ATTR_HUMI_RAW, &humi_raw))
    {
        cli_printf(ctx, "-sht30: Measure temperature fail.\n");
        return;
    }

    cli_printf(ctx, "-sht30: Humidity: %.3f %%.\n", humi_raw / 1000.0);
}

void sht30_show_temp_history(cli_ctx_t *ctx, unsigned char argc, char *argv[])
{
    char buf[SHT30_HISTORY_BUF_SIZE] = {};

    // 如果sht30设备不存在，那么不会创建fifo以及定期检测
    if(unlikely(!g_sht30_history_temp))
    {
        safe_snprintf(buf, "-sht30: Device SHT30 not exist.");
        goto done;
    }

    safe_snprintf(buf, "\nHistory order: old->new, time interval %d s\n", SHT30_MEASURE_INTERVAL / 1000);
    ev_with_spinlock(&g_sht30_history_splock)
    {
        int cnt = fifo_get_used(g_sht30_history_temp) / sizeof(double);
        for(int i = 0; i < cnt; ++ i)
        {
            double temp;
            fifo_peek(g_sht30_history_temp, i, &temp, sizeof(double));
            safe_snprintf(buf, "%.2f ℃\n", temp);
        }
    }

done:
    cli_printf(ctx, "%s\n", buf);
}

void sht30_show_humi_history(cli_ctx_t *ctx, unsigned char argc, char *argv[])
{
    char buf[SHT30_HISTORY_BUF_SIZE] = {};

    // 如果sht30设备不存在，那么不会创建fifo以及定期检测
    if(unlikely(!g_sht30_history_humi))
    {
        safe_snprintf(buf, "-sht30: Device SHT30 not exist.");
        goto done;
    }

    safe_snprintf(buf, "\nHistory order: old->new, time interval %d s\n", SHT30_MEASURE_INTERVAL / 1000);
    ev_with_spinlock(&g_sht30_history_splock)
    {
        int cnt = fifo_get_used(g_sht30_history_humi) / sizeof(double);
        for(int i = 0; i < cnt; ++ i)
        {
            double humi;
            fifo_peek(g_sht30_history_humi, i, &humi, sizeof(double));
            safe_snprintf(buf, "%.2f %%RH\n", humi);
        }
    }

done:
    cli_printf(ctx, "%s\n", buf);
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

error_code_e sht30_sensor_http_cgi_cb(json_builder_t *jb)
{
     double temp_raw = 0.0;
     double humi_raw = 0.0;

     // 暂时忽略错误，比如未接上sht30，返回0给前端
    _sht30_read_attr(SHT30_ATTR_HUMI_RAW, &humi_raw);
    _sht30_read_attr(SHT30_ATTR_TEMP_RAW, &temp_raw);

    json_object_start(jb);

    json_add_double(jb, "temperature", temp_raw / 1000.0);
    json_add_double(jb, "humidity", humi_raw / 1000.0);

    json_object_end(jb);

    return ERR_NO_ERROR;
}

error_code_e sht30_sync_http_cgi_cb(json_builder_t *jb)
{
     double temp_raw = 0.0;
     double humi_raw = 0.0;

     // 暂时忽略错误，比如未接上sht30，返回0给前端
    _sht30_read_attr(SHT30_ATTR_HUMI_RAW, &humi_raw);
    _sht30_read_attr(SHT30_ATTR_TEMP_RAW, &temp_raw);

    json_add_double(jb, "temperature", temp_raw / 1000.0);
    json_add_double(jb, "humidity", humi_raw / 1000.0);

    return ERR_NO_ERROR;
}

error_code_e sht30_history_http_cgi_cb(json_builder_t *jb)
{
    json_object_start(jb);

    if(unlikely(!g_sht30_history_temp || !g_sht30_history_humi))
        goto done;

    ev_with_spinlock(&g_sht30_history_splock)
    {
        int count = fifo_get_used(g_sht30_history_temp) / sizeof(double);
        json_array_start_with_key(jb, "labels");
        for(int i = 1; i <= count; ++ i)
            json_array_add_int(jb, i);
        json_array_end(jb);

        json_array_start_with_key(jb, "temp");
        double temp;
        for(int i = 0; i < count; ++ i)
        {
            fifo_peek(g_sht30_history_temp, i, &temp, sizeof(double));
            json_array_add_double(jb, temp);
        }
        json_array_end(jb);

        json_array_start_with_key(jb, "humi");
        double humi;
        for(int i = 0; i < count; ++ i)
        {
            fifo_peek(g_sht30_history_humi, i, &humi, sizeof(double));
            json_array_add_double(jb, humi);
        }
        json_array_end(jb);
    }

done:
    json_object_end(jb);

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

    // 初始化fifo
    g_sht30_history_temp = fifo_create(SHT30_HISTORY_SIZE * sizeof(double));
    g_sht30_history_humi = fifo_create(SHT30_HISTORY_SIZE * sizeof(double));

    // 注册定时任务检测温度湿度
    ev_thd_register(sht30_measure);
    ev_thd_run(sht30_measure);

    dbg_major("sht30 module init ok");
}

void sht30_module_exit()
{
    // 停止定时检测事件线程
    ev_thd_cancel(sht30_measure);

    // 关闭fifo
    if(likely(g_sht30_history_temp))
        fifo_destroy(&g_sht30_history_temp);
    if(likely(g_sht30_history_humi))
        fifo_destroy(&g_sht30_history_humi);

    // 注销驱动
    system("rmmod miscc_sht30");

    dbg_major("sht30 module exit ok");
}