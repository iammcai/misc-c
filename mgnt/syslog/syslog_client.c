/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    syslog_client.c
 * @brief   处理日志客户端，写入
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

#include <stdarg.h>
#include <stdio.h>

#include "syslog/syslog_internal.h"
#include "event/ev_timer.h"
#include "rpc/rpc.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

error_code_e _syslog_add_rpc_server(rpc_in_param_t *in_param, rpc_out_param_t *out_param)
{
    pfm_ensure_ret(in_param, ERR_BAD_PARAM);

    syslog_entry_t entry;
    error_code_e ret;

    ret = rpc_in_param_get(in_param, "entry", &entry, sizeof(syslog_entry_t));
    if(ERR_NO_ERROR != ret)
        return ret;

    // 打印到终端
    // 转换为可读格式
    char log[SYSLOG_ENTRY_CTX_SIZE*2] = {};
    struct tm tm_info;
    char time_str[32] = {};
    localtime_r(&entry.ts.tv_sec, &tm_info);
    strftime(time_str, 32, "%Y-%m-%d %H:%M:%S", &tm_info);
    snprintf(log, sizeof(log), "[%s][%s][%u/%s] %s\n", 
        time_str, syslog_module_str[entry.module], 
        entry.facility, syslog_facility_str[entry.facility],
        entry.ctx
    );
    safe_printf("%s", log);

    _syslog_store_buffer(&entry);       // store in buffer
}

error_code_e _syslog_add(syslog_facility_e facility, syslog_module_id_e module_id, const char *fmt, ...)
{
    pfm_ensure_ret(facility >= SYSLOG_FACILITY_EMERG && 
        facility < SYSLOG_FACILITY_MAX && 
        module_id >= 0 && 
        module_id < SYSLOG_MODULE_ID_MAX &&
        fmt,    ERR_BAD_PARAM);

    // 组装文本内容
    char temp_buf[SYSLOG_ENTRY_CTX_SIZE];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(temp_buf, SYSLOG_ENTRY_CTX_SIZE, fmt, args);
    va_end(args);
    if(n == SYSLOG_ENTRY_CTX_SIZE)
    {
        dbg_error("Syslog context out of size! Cut down to length: %d", SYSLOG_ENTRY_CTX_SIZE - 1);
        temp_buf[SYSLOG_ENTRY_CTX_SIZE - 1] = '\0';
    }

    // 构造entry准备rpc
    syslog_entry_t entry = {};
    entry.facility = facility;
    entry.module = module_id;
    realtime_get_timestamp(&entry.ts);
    memcpy(entry.ctx, temp_buf, SYSLOG_ENTRY_CTX_SIZE);

    // async rpc
    rpc_in_param_t in_param = {};
    rpc_in_param_init(&in_param);
    rpc_in_param_add(&in_param, "entry", &entry, sizeof(syslog_entry_t));
    rpc_call_func_async(PID_SYSLOG, "syslog_add_rpc_server", &in_param);

    return ERR_NO_ERROR;
}