/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    syslog_store.c
 * @brief   处理日志存储
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-11-03
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-11-03 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "syslog/syslog_internal.h"
#include "fifo/fifo.h"
#include "event/ev_timer.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// buffer日志条目最大数量
#define SYSLOG_BUFFER_MAX_COUNT         (128)

// 日志存储路径
#define SYSLOG_STORE_FLSH_FILE_PATH     "../mgnt/syslog/log/syslog.log"

// 定时器刷盘时间，30s
#define SYSLOG_FLUSH_TIMER_EXPIRED      (30*1000)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

//

/**
 * @brief       syslog store into flash
 * 
 * @note        日志写入flash
 */
static error_code_e _syslog_store_flash();

/**
 * @brief       check if syslog is important to write immediately
 * 
 * @retval      1 - y, 0 - n
 * 
 * @note        超过err等级的视为重要日志
 */
static attr_pure_inline int _syslog_is_important(syslog_entry_t *entry)
{
    if(entry)
        return entry->facility <= SYSLOG_FACILITY_ERR;
    return 0;
}

/**
 * @brief       work func for syslog flush timer
 * 
 * @note        通过rpc调用落盘，让日志操作在同个线程处理
 */
static void _syslog_flush_timer_work(void *args);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 日志fifo
static fifo_t *g_syslog_buffer = NULL;

// 日志文件句柄
static FILE *g_syslog_file = NULL;

// 定时刷盘定时器
static ev_timer_t *g_syslog_flush_timer = NULL;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static error_code_e _syslog_store_flash()
{
    // 检查buffer长度
    if(!g_syslog_buffer || !fifo_get_used(g_syslog_buffer))
        return ERR_NO_ERROR;

    // 打开文件
    g_syslog_file = fopen(SYSLOG_STORE_FLSH_FILE_PATH, "a");
    pfm_ensure_ret(g_syslog_file, ERR_SYSLOG_FLASH_FILE_NO_EXIST);

    syslog_entry_t entry;
    char log[SYSLOG_ENTRY_CTX_SIZE*2] = {};     // 用于构建写入字符串
    while(fifo_get_used(g_syslog_buffer))
    {
        fifo_read(g_syslog_buffer, (char*)&entry, sizeof(syslog_entry_t), 1);
        memset(log, 0, sizeof(log));
        // 转换为可读格式
        struct tm tm_info;
        char time_str[32] = {};
        localtime_r(&entry.ts.tv_sec, &tm_info);
        strftime(time_str, 32, "%Y-%m-%d %H:%M:%S", &tm_info);
        snprintf(log, sizeof(log), "[%s][%s][%u/%s] %s\n", 
            time_str, syslog_module_str[entry.module], 
            entry.facility, syslog_facility_str[entry.facility],
            entry.ctx
        );
        fwrite(log, 1, strlen(log), g_syslog_file);
    }

    // 强制落盘
    fflush(g_syslog_file);
    fsync(fileno(g_syslog_file));

    // 关闭
    fclose(g_syslog_file);
    g_syslog_file = NULL;

    return ERR_NO_ERROR;
}

error_code_e _syslog_flush_rpc_server(rpc_in_param_t *in_param, rpc_out_param_t *out_param)
{
    return _syslog_store_flash();
}

static void _syslog_flush_timer_work(void *args)
{
    rpc_out_param_t out_param = {};
    error_code_e ret;

    ret = rpc_call_func_sync(PID_SYSLOG, "syslog_flush_rpc_server", NULL, &out_param, RPC_SYNC_CALL_WAIT_FOREVER);
    if(ERR_NO_ERROR != ret)
        dbg_error("rpc call syslog flush fail, err: %d", ret);

    ev_timer_start(g_syslog_flush_timer);       // 重新启动定时
}

error_code_e _syslog_store_flash_timer_start()
{
    g_syslog_flush_timer = ev_timer_create("syslog_flush", SYSLOG_FLUSH_TIMER_EXPIRED, _syslog_flush_timer_work, NULL);
    pfm_ensure_ret(g_syslog_flush_timer, ERR_EV_TIMER_CREATE_FAIL);

    ev_timer_start(g_syslog_flush_timer);

    return ERR_NO_ERROR;
}

void _syslog_store_flash_timer_end()
{
    ev_timer_stop(g_syslog_flush_timer);
}

error_code_e _syslog_store_buffer(syslog_entry_t *entry)
{
    pfm_ensure_ret(entry, ERR_BAD_PARAM);

    error_code_e ret = ERR_NO_ERROR;

    if(unlikely(!g_syslog_buffer))
        g_syslog_buffer = fifo_create(sizeof(syslog_entry_t) * SYSLOG_BUFFER_MAX_COUNT);

    pfm_ensure_ret(g_syslog_buffer, ERR_SYSLOG_BUFFER_NO_EXIST);

    if(unlikely(fifo_is_full(g_syslog_buffer)))
        _syslog_store_flash();

    if(sizeof(syslog_entry_t) != fifo_write(g_syslog_buffer, entry, sizeof(syslog_entry_t)))
    {
        ret = ERR_SYSLOG_BUFFER_WRITE_FAIL;
        dbg_error("syslog write into buffer fail");
        goto done;
    }

    // 如果是紧急的日志，那么立即写入flash
    if(_syslog_is_important(entry))
        _syslog_store_flash();

done:
    return ret;
}