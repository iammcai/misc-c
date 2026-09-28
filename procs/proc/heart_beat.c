/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    heart_beat.c
 * @brief   进程心跳实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-24
 * @version 1.0
 *
 * @note    提供进程心跳检测相关API
 *
 * @history
 *   1.0 | 2026-09-24 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/prctl.h>

#include "plat/atom.h"
#include "proc/heart_beat.h"
#include "proc/proc.h"
#include "ipc/uds/uds.h"
#include "event/ev_timer.h"
#include "mp/mp.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 用于心跳的unix socket path
#define PROC_HEART_BEAT_SOCK_PATH_PREFIX    "heart_beat_"
// 最大长度
#define PROC_HEART_BEAT_SOCK_PATH_LEN       (16)

// 心跳间隔，5s
#define PROC_HEART_BEAT_INTERVAL    (5000)

// 打印hb格式，pid last_hb
#define PROC_HB_FMT_HEAD            "%-12s%-12s\n"
// 打印hb数据格式，pid last_hb
#define PROC_HB_FMT_DATA            "%-12d%-12lu\n"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 心跳消息
typedef struct{
    proc_id_e pid;      // process id
    uint64_t ts;        // time stamp
}hb_msg_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       hb thread create ...
 */
static attr_force_inline void _proc_heart_beat_work();

/**
 * @brief       hb thread exit and so on...
 */
static attr_force_inline void _proc_heart_beat_work_cancel();

/**
 * @brief       heartbeat thread workfunc
 */
static void* _proc_heart_beat_routine(void *args);

/**
 * @brief       heartbeat check workfunc
 */
static void* _proc_heart_beat_chk_routine(void *args);

/**
 * @brief       send heartbeat msg
 */
static attr_force_inline void _proc_heart_beat_send();

/**
 * @brief       core process receive heartbeat msg
 */
static void _proc_heart_beat_recv();

/**
 * @brief       core update other process' heart beat
 * 
 * @param[in]   pid     - process id
 * @param[in]   ts      - timestamp
 */
static attr_force_inline void _proc_heart_beat_update(proc_id_e pid, uint64_t ts);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 非core发送心跳的uds
static uds_mgnt_t *g_uds_mgnt = NULL;

// core接收心跳uds数组
static uds_mgnt_t* *g_usd_mgnter_array = NULL;

// core记录其它进程心跳时间戳数组
static uint64_t *g_heart_beat_note_array = NULL;

// 心跳工作线程
static pthread_t g_heart_beat_tid = 0;
// 工作标志
static ATOMIC_UINT8_T g_heart_beat_running;

// core心跳检测线程
static pthread_t g_heart_beat_chk_tid = 0;
// 工作标志
static ATOMIC_UINT8_T g_heart_beat_chk_running;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static void* _proc_heart_beat_routine(void *args)
{
    // set thread name
    char name[16] = {};
    snprintf(name, sizeof(name)-1, "hb_%d", proc_id_get());
    prctl(PR_SET_NAME, name);

    ATOM_STORE(&g_heart_beat_running, 1, MORDER_RELAXED);
    while(ATOM_LOAD(&g_heart_beat_running, MORDER_RELAXED))
    {
        if(!proc_current_is_core())
        {
            _proc_heart_beat_send();
            usleep(PROC_HEART_BEAT_INTERVAL * 1000);
        }
        else
        {
            _proc_heart_beat_recv();
            usleep((PROC_HEART_BEAT_INTERVAL >> 2) * 1000);     // 1/4检测时间
        }
    }

    return NULL;
}

static void* _proc_heart_beat_chk_routine(void *args)
{
    // set thread name
    prctl(PR_SET_NAME, "hb_check");

    // 等待 2 period再开始
    ATOM_STORE(&g_heart_beat_chk_running, 1, MORDER_RELAXED);
    usleep(2 * PROC_HEART_BEAT_INTERVAL * 1000);
    while(ATOM_LOAD(&g_heart_beat_chk_running, MORDER_RELAXED))
    {
        //dbg_always("core check hb");
        uint64_t curr_time = mono_time_get();
        for(int i = PID_CORE + 1; i < PID_CNT; ++ i)
        {
            uint64_t last_hb_ts = ATOM_LOAD(&g_heart_beat_note_array[i], MORDER_RELAXED);
            if((curr_time > last_hb_ts) && (curr_time - last_hb_ts > (PROC_HEART_BEAT_INTERVAL * 3)))   // 超出3个周期，认为失联
            {
                dbg_always("heart beat loss, pid %d", i);
                // TODO: 其它进阶处理
            }
        }

        // 1/4个周期检测一次
        usleep((PROC_HEART_BEAT_INTERVAL >> 2) * 1000);
    }
}

static inline void _proc_heart_beat_send()
{
    if(proc_current_is_core())
        return;

    hb_msg_t msg = {
        .pid = proc_id_get(),
        .ts = mono_time_get(),
    };
    uds_send(g_uds_mgnt, &msg, sizeof(msg));
    //dbg_always("@@@ send msg by uds");
}

static void _proc_heart_beat_recv()
{
    if(!proc_current_is_core())
        return;

    hb_msg_t msg = {};
    for(int pid = PID_CORE+1; pid < PID_CNT; ++ pid)
    {
        if(uds_try_recv(g_usd_mgnter_array[pid], &msg, sizeof(msg)))
        {
            //dbg_always("### recv msg by uds, pid %d", pid);
            _proc_heart_beat_update(msg.pid, msg.ts);
        }
    }
}

static inline void _proc_heart_beat_update(proc_id_e pid, uint64_t ts)
{
    pfm_ensure_ret_void(pid > PID_CORE && pid < PID_CNT && proc_current_is_core());
    ATOM_STORE(&g_heart_beat_note_array[pid], ts, MORDER_RELAXED);
}

void proc_heart_beat_module_init()
{
    char sock_path[PROC_HEART_BEAT_SOCK_PATH_LEN] = {};
    if(!proc_current_is_core())     // 非core创建unix socket用于ipc发送
    {
        snprintf(sock_path, sizeof(sock_path), "%s%d", PROC_HEART_BEAT_SOCK_PATH_PREFIX, proc_id_get());
        g_uds_mgnt = uds_mgnt_create(sock_path, 0);
        pfm_ensure_ret_void(g_uds_mgnt);
    }
    else        // core创建多个uds用于ipc接收
    {
        void *mem_trunk = mp_calloc(1, PID_CNT * sizeof(uds_mgnt_t*) + PID_CNT * sizeof(uint64_t));
        if(unlikely(!mem_trunk))
        {
            dbg_always("no mem for core init hb");
            return;
        }
        g_usd_mgnter_array = (uds_mgnt_t**)mem_trunk;
        g_heart_beat_note_array = (uint64_t*)((char*)g_usd_mgnter_array + PID_CNT * sizeof(uds_mgnt_t*));
        for(int pid = PID_CORE+1; pid < PID_CNT; ++ pid)
        {
            snprintf(sock_path, sizeof(sock_path), "%s%d", PROC_HEART_BEAT_SOCK_PATH_PREFIX, pid);
            g_usd_mgnter_array[pid] = uds_mgnt_create(sock_path, 1);
        }
    }

    _proc_heart_beat_work();
}

void proc_heart_beat_module_exit()
{
    // 结束线程
    _proc_heart_beat_work_cancel();
    // 资源回收
    if(!proc_current_is_core())
        uds_mgnt_destroy(g_uds_mgnt);
    else            // core
    {
        for(int i = PID_CORE + 1; i < PID_CNT; ++ i)
            uds_mgnt_destroy(g_usd_mgnter_array[i]);
        mp_free(g_usd_mgnter_array, PID_CNT * sizeof(uds_mgnt_t*) + PID_CNT * sizeof(uint64_t));    // 实际回收mem_trunk
    }
}

static inline void _proc_heart_beat_work()
{
    // 创建工作线程
    pthread_create(&g_heart_beat_tid, NULL, _proc_heart_beat_routine, NULL);

    // core创建检测线程
    if(proc_current_is_core())
        pthread_create(&g_heart_beat_chk_tid, NULL, _proc_heart_beat_chk_routine, NULL);
}

static inline void _proc_heart_beat_work_cancel()
{
    // core检测结束
    if(proc_current_is_core())
    {
        ATOM_STORE(&g_heart_beat_chk_running, 0, MORDER_RELAXED);
        pthread_join(g_heart_beat_chk_tid, NULL);
    }

    // 工作线程（收发hb_msg）结束
    ATOM_STORE(&g_heart_beat_running, 0, MORDER_RELAXED);
    pthread_join(g_heart_beat_tid, NULL);
}

void proc_heart_beat_show_cli_cb(unsigned char argc, char *argv[])
{
    safe_printf("\nProcess heart beat info\n-----------------------\n");
    safe_printf(PROC_HB_FMT_HEAD PROC_HB_FMT_HEAD, 
        "proc_id", "last_hb",
        "-------", "-------"
    );
    uint64_t curr_time = mono_time_get();
    for(int i = PID_CORE + 1; i < PID_CNT; ++ i)
        safe_printf(PROC_HB_FMT_DATA, i, 
            curr_time - ATOM_LOAD(&g_heart_beat_note_array[i], MORDER_RELAXED));

    safe_printf("-----------------------\n");
}