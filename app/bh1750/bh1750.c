/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    bh1750.c
 * @brief   bh1750用户态程序
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-11-10
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-11-10 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "bh1750/miscc_bh1750.h"
#include "bh1750/bh1750.h"
#include "plat/atom.h"
#include "led/led.h"
#include "swdg/swdg.h"
#include "syslog/syslog_api.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// IIO设备前缀目录
#define IIO_BASE_DIR    "/sys/bus/iio/devices"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 文件路径长度
#define BH1750_FILE_PATH_SIZE       (256)

// iio暴露出的raw data文件
#define BH1750_IIO_ATTR_RAW         "in_illuminance_raw"

// iio暴露出的scale文件
#define BH1750_IIO_ATTR_SCALE       "in_illuminance_scale"

// bh1750轮询间隔，1s
#define BH1750_POLL_INTERVAL        (1000)

#define LUX_THRESHOLD_ON            (30.0)  // 低于此值开灯
#define LUX_THRESHOLD_OFF           (80.0)  // 高于此值关灯

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       获取bh1750对应iio设备的属性的文件路径
 * 
 * @param[in]   attr            - 属性名
 * @param[out]  output_path     - find result
 * @param[in]   max_size        - size of output_path param
 * 
 * @note        例如获取原始测量值的路径，xxx/in_illuminance_raw
 */
static error_code_e _bh1750_find_device_attr_path(const char *attr, char *output_path, size_t max_size);

/**
 * @brief       routine for bh1750 poll
 */
static void* _bh1750_poll(void *args);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// bh1750 iio raw file
static char bh1750_raw_path[BH1750_FILE_PATH_SIZE] = {};
// bh1750 iio scale file
static char bh1750_scale_path[BH1750_FILE_PATH_SIZE] = {};

// 轮询光照线程
static pthread_t g_bh1750_poll_tid;
// run flag
static ATOMIC_UINT8_T g_bh1750_poll_running = 0;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static error_code_e _bh1750_find_device_attr_path(const char *attr, char *output_path, size_t max_size)
{
    DIR *dir;
    struct dirent *entry;
    char name_path[256];
    char name_buf[64];
    FILE *fp;

    pfm_ensure_ret(attr && output_path && max_size, ERR_BAD_PARAM);

    // 打开base目录
    dir = opendir(IIO_BASE_DIR);
    if(!dir)
    {
        perror("opendir iio");
        return ERR_SYS_API_ERROR;
    }

    while((entry = readdir(dir)))
    {
        // 只处理 iio:deviceX 目录，跳过 . 和 .. 以及 trigger 等
        if (strncmp(entry->d_name, "iio:device", 10) != 0)
            continue;

        // 拼接 name 文件路径: /sys/bus/iio/devices/iio:deviceX/name
        snprintf(name_path, sizeof(name_path), "%s/%s/name", IIO_BASE_DIR, entry->d_name);
        fp = fopen(name_path, "r");
        if(!fp)
            continue;

        if (fgets(name_buf, sizeof(name_buf), fp)) {
            // 去除末尾换行符
            name_buf[strcspn(name_buf, "\n")] = '\0';

            // 匹配名称
            if (strcmp(name_buf, BH1750_IIO_DEVICE_NAME) == 0)
            {
                fclose(fp);
                closedir(dir);
                // 如果传了 attr，就拼接属性路径；否则返回设备根目录
                if (attr && attr[0] != '\0')
                    snprintf(output_path, max_size, "%s/%s/%s", IIO_BASE_DIR, entry->d_name, attr);
                else
                    snprintf(output_path, max_size, "%s/%s", IIO_BASE_DIR, entry->d_name);
                return ERR_NO_ERROR;
            }
        }
        fclose(fp);
    }

    closedir(dir);
    return ERR_BAD_PARAM;   // 没找到
}

static void* _bh1750_poll(void *args)
{
    const char* name = "bh1750_poll";
    prctl(PR_SET_NAME, name);
    swdg_register(name, BH1750_POLL_INTERVAL * 3);

    double lux;

    ATOM_STORE(&g_bh1750_poll_running, 1, MORDER_RELAXED);
    while(ATOM_LOAD(&g_bh1750_poll_running, MORDER_RELAXED))
    {
        swdg_feed();
        if(ERR_NO_ERROR == bh1750_lux_read(&lux))
        {
            led_status_e led = led_status_get();
            if(led == LED_STATUS_OFF && lux <= LUX_THRESHOLD_ON)
            {
                led_turn_on();
                SYSLOG_NOTICE(SYSLOG_MODULE_ID_BH1750, "Light is dark, turn ON LED. Current: %.2f lux.", lux);
            }
            else if(led == LED_STATUS_ON && lux >= LUX_THRESHOLD_OFF)
            {
                led_turn_off();
                SYSLOG_NOTICE(SYSLOG_MODULE_ID_BH1750, "Light is bright, turn OFF LED. Current: %.2f lux.", lux);
            }
        }
        usleep(BH1750_POLL_INTERVAL * 1000);
    }

    swdg_deregister(name);

    return NULL;
}

void bh1750_module_init()
{
    // 加载驱动，忽略错误
    system("modprobe industrialio");
    system("insmod ../driver/bh1750/miscc_bh1750.ko");

    // 驱动有120ms延时，这里也加上防止未加载就去找文件
    usleep(300 * 1000);

    // 查找对应设备
    if(ERR_NO_ERROR != _bh1750_find_device_attr_path(BH1750_IIO_ATTR_RAW, bh1750_raw_path, sizeof(bh1750_raw_path)))
    {
        dbg_error("find bh1750 raw file fail");
        return;
    }
    if(ERR_NO_ERROR != _bh1750_find_device_attr_path(BH1750_IIO_ATTR_SCALE, bh1750_scale_path, sizeof(bh1750_scale_path)))
    {
        dbg_error("find bh1750 scale file fail");
        return;
    }

    // 启动轮询线程
    pthread_create(&g_bh1750_poll_tid, NULL, _bh1750_poll, NULL);
    while(!ATOM_LOAD(&g_bh1750_poll_running, MORDER_RELAXED))   usleep(100 * 1000);
}

void bh1750_module_exit()
{
    // 结束轮询线程，1->0
    if(ATOM_LOAD(&g_bh1750_poll_running, MORDER_RELAXED))
    {
        ATOM_STORE(&g_bh1750_poll_running, 0, MORDER_RELAXED);
        pthread_join(g_bh1750_poll_tid, NULL);
    }

    // 注销驱动
    system("rmmod miscc_bh1750");
}

error_code_e bh1750_lux_read(double *lux)
{
    pfm_ensure_ret(lux, ERR_BAD_PARAM);
    
    FILE *g_bh1750_iio_raw_fp = fopen(bh1750_raw_path, "r");
    FILE *g_bh1750_iio_scale_fp = fopen(bh1750_scale_path, "r");
    pfm_ensure_done(g_bh1750_iio_raw_fp && g_bh1750_iio_scale_fp);

    double raw, scale;
    fscanf(g_bh1750_iio_raw_fp, "%lf", &raw);
    fscanf(g_bh1750_iio_scale_fp, "%lf", &scale);

    *lux = raw * scale;

done:
    fclose(g_bh1750_iio_raw_fp);
    fclose(g_bh1750_iio_scale_fp);
    return ERR_NO_ERROR;
}

void bh1750_light_dump_cli_cb(cli_ctx_t *ctx, unsigned char argc, char *argv[])
{
    double lux;
    error_code_e ret = bh1750_lux_read(&lux);
    if(ERR_NO_DEV == ret)
    {
        cli_printf(ctx, "-bh1750: Error, device bh1750 not exist\n");
    }
    else if(ERR_NO_ERROR != ret)
    {
        cli_printf(ctx, "-bh1750: Error, device bh1750 measure light fail\n");
    }
    else
    {
        cli_printf(ctx, "%.2f lux.\n", lux);
    }
}