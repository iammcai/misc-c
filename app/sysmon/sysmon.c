/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    sysmon.c
 * @brief   系统监控实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-08-12
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-08-12 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <unistd.h>
#include <stdio.h>

#include "sysmon/sysmon.h"
#include "plat/atom.h"
#include "event/ev_thread.h"
#include "event/ev_lock.h"
#include "cli/cli.h"
#include "syslog/syslog.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// CPU数量，树莓派5B为4核CPU，整体+1
#define SYSMON_CPU_COUNTS       (5)

// CPU利用率计算时间间隔，1s，计算本身要花1s
#define SYSMON_CPU_USAGE_CAL_INTERVAL       (1000)

// CPU利用率高告警阈值
#define SYSMON_CPU_TOTAL_USAGE_HIGH_THRESHOLD       (25)
#define SYSMON_CPU_CORE_USAGE_HIGH_THRESHOLD        (75)
#define SYSMON_CPU_HIGH_COUNTS_THRESHOLD            (5)

// CPU利用率打印表头格式，cpu usage
#define SYSMON_CPU_USAGE_FMT_HEAD       "%-10s%-10s\n"
// CPU利用率打印数据格式
#define SYSMON_CPU_USAGE_FMT_DATA       "%-10s%-10.2f\n"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// cpu stat结构定义
typedef struct{
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;
    unsigned long long guest;
    unsigned long long guest_nice;
}sysmon_per_cpu_stat_t;

// 多核CPU stat
typedef struct{
    sysmon_per_cpu_stat_t cpu_stat[SYSMON_CPU_COUNTS];
}sysmon_cpu_stat_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       read cpu stat
 * 
 * @param[out]  stat    - cpu stat
 */
static void _sysmon_cpu_stat_read(sysmon_cpu_stat_t *stat)
{
    pfm_ensure_ret_void(stat);

    // /proc目录文件每次都重新打开，获取最新快照
    FILE *fp = fopen("/proc/stat", "r");
    if(unlikely(!fp))
    {
        dbg_error("fopen() error");
        return;
    }

    memset(stat, 0, sizeof(sysmon_cpu_stat_t));
    char line[256] = {};
    sysmon_per_cpu_stat_t *p_stat = NULL;

    // 逐行读取文件
    while(fgets(line, sizeof(line), fp))
    {
        // 仅处理cpu开头的行
        if(0 != strncmp(line, "cpu", 3))
            continue;

        // 分发stat指针
        if(line[3] == ' ')
            p_stat = &stat->cpu_stat[0];
        else if(line[3] == '0')
            p_stat = &stat->cpu_stat[1];
        else if(line[3] == '1')
            p_stat = &stat->cpu_stat[2];
        else if(line[3] == '2')
            p_stat = &stat->cpu_stat[3];
        else if(line[3] == '3')
            p_stat = &stat->cpu_stat[4];
        else
        {
            dbg_error("fgets() error");
            goto done;
        }

        int n = sscanf(line, "%*s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
            &p_stat->user, &p_stat->nice, &p_stat->system, &p_stat->idle, &p_stat->iowait,
            &p_stat->irq, &p_stat->softirq, &p_stat->steal, &p_stat->guest, &p_stat->guest_nice
        );
        if(n != 10)
        {
            dbg_error("sscanf() fail");
            goto done;
        }
    }

done:
    fclose(fp);
}

/**
 * @brief       cb for ev thd, cal cpu usage
 */
static void _sysmon_cpu_usage_cal(void *args);

/**
 * @brief       cb for cli: show cpu usage
 */
static void* _sysmon_show_cpu_usage_cli_hook(unsigned char argc, char *argv[]);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 全局CPU利用率，x%。cpu, cpu0, cpu1, cpu2, cpu3
static double g_sysmon_cpu_usages[SYSMON_CPU_COUNTS] = {};
// 全局CPU利用率高检测次数
static int g_sysmon_cpu_usages_high[SYSMON_CPU_COUNTS] = {};

// 利用率锁
static ev_spinlock_t g_sysmon_cpu_usage_lock = EV_SPINLOCK_INITIALIZER;

// 事件线程，执行CPU利用率更新
declare_ev_thd(cpu_usage_cal, _sysmon_cpu_usage_cal, NULL, SYSMON_CPU_USAGE_CAL_INTERVAL)

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static void _sysmon_cpu_usage_cal(void *args)
{
    sysmon_cpu_stat_t prev = {}, next = {};

    _sysmon_cpu_stat_read(&prev);
    sleep(1);       // 等待1s
    _sysmon_cpu_stat_read(&next);

    ev_with_spinlock(&g_sysmon_cpu_usage_lock)
    {
        for(size_t i = 0; i < SYSMON_CPU_COUNTS; ++ i)
        {
            // 空闲jiffies
            unsigned long long prev_idle = prev.cpu_stat[i].idle + prev.cpu_stat[i].iowait;
            unsigned long long next_idle = next.cpu_stat[i].idle + next.cpu_stat[i].iowait;
            // 总jiffies
            unsigned long long prev_total = prev.cpu_stat[i].user + prev.cpu_stat[i].nice + prev.cpu_stat[i].system
                + prev.cpu_stat[i].idle + prev.cpu_stat[i].iowait + prev.cpu_stat[i].irq + prev.cpu_stat[i].softirq
                + prev.cpu_stat[i].steal + prev.cpu_stat[i].guest + prev.cpu_stat[i].guest_nice;
            unsigned long long next_total = next.cpu_stat[i].user + next.cpu_stat[i].nice + next.cpu_stat[i].system
                + next.cpu_stat[i].idle + next.cpu_stat[i].iowait + next.cpu_stat[i].irq + next.cpu_stat[i].softirq
                + next.cpu_stat[i].steal + next.cpu_stat[i].guest + next.cpu_stat[i].guest_nice;

            unsigned long long dt = next_total - prev_total;
            unsigned long long di = next_idle - prev_idle;

            // 更新数组
            g_sysmon_cpu_usages[i] = dt == 0 ? 0.0 : (double)(dt - di)/dt * 100.0;
            // 记录cpu high次数
            if((i == 0 && g_sysmon_cpu_usages[i] >= SYSMON_CPU_TOTAL_USAGE_HIGH_THRESHOLD) || 
                (i != 0 && g_sysmon_cpu_usages[i] >= SYSMON_CPU_CORE_USAGE_HIGH_THRESHOLD))
                g_sysmon_cpu_usages_high[i] ++;
            else
                g_sysmon_cpu_usages_high[i] = 0;
    
            if(g_sysmon_cpu_usages_high[i] >= SYSMON_CPU_HIGH_COUNTS_THRESHOLD)
            {
                g_sysmon_cpu_usages_high[i] = 0;
                char buf[SYSLOG_ENTRY_CTX_SIZE] = {};
                if(i == 0)
                    snprintf(buf, sizeof(buf), "cpu total usage %.2f %% is ltoo high, please check", g_sysmon_cpu_usages[i]);
                else
                    snprintf(buf, sizeof(buf), "cpu core %d usage %.2f %% is ltoo high, please check", i - 1, g_sysmon_cpu_usages[i]);
                syslog_warn(SYSLOG_MODULE_SYSMON, buf);
            }
        }
    }
}

static void* _sysmon_show_cpu_usage_cli_hook(unsigned char argc, char *argv[])
{
    double temp[SYSMON_CPU_COUNTS] = {};
    ev_with_spinlock(&g_sysmon_cpu_usage_lock)
        memcpy(temp, g_sysmon_cpu_usages, sizeof(double)*SYSMON_CPU_COUNTS);

    safe_printf("\r\n");
    safe_printf(SYSMON_CPU_USAGE_FMT_HEAD, "cpu", "usage(%)");
    safe_printf(SYSMON_CPU_USAGE_FMT_HEAD, "---", "--------");

    for(size_t i = 0; i < SYSMON_CPU_COUNTS; ++ i)
    {
        char cpu_str[8] = {};
        if(i > 0)
            snprintf(cpu_str, sizeof(cpu_str), "cpu_%d", i-1);
        else
            snprintf(cpu_str, sizeof(cpu_str), "cpu");
        safe_printf(SYSMON_CPU_USAGE_FMT_DATA, cpu_str, temp[i]);
    }

    safe_printf("\r\n");

    return NULL;
}

error_code_e sysmon_cpu_usage_http_cgi_cb(char *buf, size_t buf_size)
{
    double temp[SYSMON_CPU_COUNTS] = {};
    ev_with_spinlock(&g_sysmon_cpu_usage_lock)
        memcpy(temp, g_sysmon_cpu_usages, sizeof(double)*SYSMON_CPU_COUNTS);

    memset(buf, 0, buf_size);
    snprintf(buf, buf_size, 
        "{\"total\":\"%.2f\",\"cpu0\":\"%.2f\",\"cpu1\":\"%.2f\",\"cpu2\":\"%.2f\",\"cpu3\":\"%.2f\"}",
        temp[0], temp[1], temp[2], temp[3], temp[4]);

    return ERR_NO_ERROR;
}

void sysmon_module_init()
{
    // 启动事件线程来持续更新CPU利用率
    ev_thd_register(cpu_usage_cal);
    ev_thd_run(cpu_usage_cal);
    // 立即计算一次
    ev_thd_wake(cpu_usage_cal);

    // 注册cli展示利用率
    cli_register("show cpu usage", "dump cpu usage", NULL, _sysmon_show_cpu_usage_cli_hook);

    dbg_major("sysmon module init ok");
}

void sysmon_module_deinit()
{
    ev_thd_cancel(cpu_usage_cal);

    dbg_major("sysmon module deinit ok");
}