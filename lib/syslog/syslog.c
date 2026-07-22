/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    syslog.c
 * @brief   系统日志实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-07
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-07 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <time.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include "syslog/syslog.h"
#include "plat/debug.h"
#include "msg_q/msg_q.h"
#include "plat/atom.h"
#include "cli/cli.h"
#include "event/ev_thread.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 日志内容的缓冲区长度，保持8字节对齐
#define SYSLOG_ENTRY_CTX_SIZE   (108)
// 日志环形缓冲区大小，必须是2次幂
#define SYSLOG_RING_SIZE        (64)
_Static_assert((SYSLOG_RING_SIZE & (SYSLOG_RING_SIZE-1)) == 0, "Ring size must be power of 2");
// 位运算取代耗时的%
#define syslog_ring_mask(n)     ((n) & (SYSLOG_RING_SIZE-1))

// 格式化日志的长度
#define SYSLOG_STR_SIZE         (160)

// 日志消息队列长度
#define SYSYLOG_MSGQ_SIZE       (128)

// 日志格式
#define SYSLOG_STR_FMT          "%-44s%s"       // ([time][module][facility]) (ctx)

// 日志文件相对路径
#define SYSLOG_FLUSH_PATH       "../lib/syslog/syslog.log"
// 写入文件的日志等级阈值
#define SYSLOG_FLUSH_FACILITY_THRESHOLD     SYSLOG_FACILITY_WARN
// 写入文件的长度阈值
#define SYSLOG_FLUSH_OFFSET_THRESHOLD       ((SYSLOG_RING_SIZE >> 1) * SYSLOG_STR_SIZE)
// 刷盘间隔，单位ms
#define SYSLOG_FLUSH_INTERVAL_MS    (10000)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 日志条目定义，占128B
typedef struct{
    struct timespec ts;                 // 时间戳，树莓派5上16B
    uint32_t module : 5;                // 模块
    uint32_t facility : 3;              // 等级，和模块共用4B
    char ctx[SYSLOG_ENTRY_CTX_SIZE];    // 日志内容
} attr_aligned(8) syslog_entry_t;

_Static_assert(sizeof(syslog_entry_t) == 128, "sizeof(syslog_entry_t) must be 128B");

// 可视化日志条目定义
typedef struct{
    char str[SYSLOG_STR_SIZE];
}syslog_str_t;

// 日志环形缓冲区定义
typedef struct{
    syslog_str_t entries[SYSLOG_RING_SIZE];
    ev_rwlock_t lock;
    uint32_t head;
    uint32_t tail;

    int fd;         // 写入文件对应的fd
    char flush_ctx[SYSLOG_RING_SIZE * SYSLOG_STR_SIZE]; // 需要写入文件的缓冲区
    uint32_t offset;        // flush长度
}syslog_ring_t;

// 日志输出方向枚举
typedef enum{
    SYSLOG_DIRECTION_NONE = 0,
    SYSLOG_DIRECTION_TERMINAL = 1 << 0,
    SYSLOG_DIRECTION_FLASH = 1 << 1,
}syslog_direction_e;

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 日志消息队列
declare_msg_q(syslog, SYSYLOG_MSGQ_SIZE, sizeof(syslog_entry_t));

// 日志环形缓冲
static syslog_ring_t g_syslog_ring = {};

// 日志消费线程
static pthread_t syslog_consumer_tid;

// 日志输出，默认到终端和flash
static ATOMIC_UINT8_T g_syslog_direction = SYSLOG_DIRECTION_TERMINAL | SYSLOG_DIRECTION_FLASH;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       cli hook for get syslog direction
 */
static void* _syslog_direction_get_cli_hook(unsigned char argc, char* argv[])
{
    uint8_t flag = ATOM_LOAD(&g_syslog_direction, MORDER_ACQUIRE);
    safe_printf(
        "terminal %d, flash %d\n", 
        0 != (g_syslog_direction & SYSLOG_DIRECTION_TERMINAL),
        0 != (g_syslog_direction & SYSLOG_DIRECTION_FLASH)
    );
}

/**
 * @brief       cli hook for set syslog direction
 */
static void* _syslog_direction_cli_hook(unsigned char argc, char* argv[])
{
    unsigned char i = 0;
    int dirs[2] = {
        0 != (g_syslog_direction & SYSLOG_DIRECTION_TERMINAL),
        0 != (g_syslog_direction & SYSLOG_DIRECTION_FLASH)
    };       // 当前设置

    if(argv[0])
    {
        dirs[0] = cli_param_parse_str_2_u32(argv[0]);
        if(!(0 == dirs[0] || 1 == dirs[0]))
        {
            safe_printf("-syslog: error param %s for -t\n", argv[0]);
            return NULL;
        }
    }

    if(argv[1])
    {
        dirs[1] = cli_param_parse_str_2_u32(argv[1]);
        if(!(0 == dirs[1] || 1 == dirs[1]))
        {
            safe_printf("-syslog: error param %s for -f\n", argv[1]);
            return NULL;
        }
    }

    uint8_t flag = (dirs[0] ? SYSLOG_DIRECTION_TERMINAL : 0) | (dirs[1] ? SYSLOG_DIRECTION_FLASH : 0);
    ATOM_STORE(&g_syslog_direction, flag, MORDER_RELEASE);
}

/**
 * @brief       check if syslog ring buffer full
 */
static attr_force_inline int _syslog_ring_full(void)
{
    return (g_syslog_ring.head - g_syslog_ring.tail) == SYSLOG_RING_SIZE;
}

/**
 * @brief       cal valid size of syslog ring buffer
 * 
 * @retval      ring buffer size
 */
static attr_force_inline int _syslog_ring_size(void)
{
    return (g_syslog_ring.head - g_syslog_ring.tail);
}

/**
 * @brief       analyze a syslog entry
 * 
 * @param[in]   entry   - syslog entry
 */
static void _syslog_analyze(syslog_entry_t *entry);

/**
 * @brief       syslog consumer routine
 */
static void* _syslog_consumer(void *args)
{
    dbg_major("syslog consumer start working");
    while(1)
    {
        syslog_entry_t entry = {};
        msg_q_pop(syslog, sizeof(syslog_entry_t), msg_q_wait_forever, &entry);
        _syslog_analyze(&entry);
    }
    return NULL;
}

/**
 * @brief       flush syslog flush buffer into file
 * 
 * @param[in]   need_lock   - if need lock inside
 */
static void _syslog_flush_file(int need_lock)
{
    static char s_flush_ctx[SYSLOG_RING_SIZE * SYSLOG_STR_SIZE] = {};
    uint32_t offset = 0;

    if(need_lock)   ev_wr_lock(&g_syslog_ring.lock);
    {
        if(g_syslog_ring.fd >= 0 && g_syslog_ring.offset > 0)
        {
            memcpy(s_flush_ctx, g_syslog_ring.flush_ctx, g_syslog_ring.offset);
            offset = g_syslog_ring.offset;
            g_syslog_ring.offset = 0;
        }
    }
    if(need_lock)   ev_wr_unlock(&g_syslog_ring.lock);

    // 锁外执行IO
    if(offset > 0)
        write(g_syslog_ring.fd, s_flush_ctx, offset);
}

/**
 * @brief       workfunc of ev thd, flush file
 */
static void _syslog_flush_thd_wf(void *args)
{
    _syslog_flush_file(1);  // 内部需要上锁
}

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

// 刷盘线程，10s工作一次
declare_ev_thd(syslog_flush, _syslog_flush_thd_wf, NULL, SYSLOG_FLUSH_INTERVAL_MS);

static void _syslog_analyze(syslog_entry_t *entry)
{
    pfm_ensure_ret_void(entry);

    syslog_str_t str = {};
    uint32_t head_mask;

    // 时间戳转换
    struct tm tm_info;
    char time_str[32] = {};
    localtime_r(&entry->ts.tv_sec, &tm_info);
    strftime(time_str, 32, "%Y-%m-%d %H:%M:%S", &tm_info);

    // [time] [module] [facility] ctx
    char prefix[64] = {};
    snprintf(prefix, 64, "[%s][%s][%d/%s]", time_str, syslog_module_str[entry->module], entry->facility, syslog_facility_str[entry->facility]);

    // 设置到缓冲区
    ev_with_wrlock(&g_syslog_ring.lock)
    {
        // 缓冲区满了的话，挪出一条
        if(_syslog_ring_full())
            g_syslog_ring.tail ++;

        head_mask = syslog_ring_mask(g_syslog_ring.head);
        memset(&g_syslog_ring.entries[head_mask], 0, sizeof(syslog_str_t));
        snprintf(g_syslog_ring.entries[head_mask].str, SYSLOG_STR_SIZE, SYSLOG_STR_FMT, prefix, entry->ctx);

        // 输出到终端
        uint8_t flag = ATOM_LOAD(&g_syslog_direction, MORDER_ACQUIRE);
        if((flag & SYSLOG_DIRECTION_TERMINAL))
            safe_printf("%s\n", g_syslog_ring.entries[head_mask].str);
        // 输出到flash
        if((flag & SYSLOG_DIRECTION_FLASH) && entry->facility <= SYSLOG_FLUSH_FACILITY_THRESHOLD)
        {
            uint32_t remain = sizeof(g_syslog_ring.flush_ctx) - g_syslog_ring.offset;
            int written = snprintf(g_syslog_ring.flush_ctx + g_syslog_ring.offset, remain, "%s\n", g_syslog_ring.entries[head_mask].str);
            if(written > 0 && written < remain)
                g_syslog_ring.offset += written;
            // 长度超出，直接触发刷盘，内部无需加锁
            if(g_syslog_ring.offset >= SYSLOG_FLUSH_OFFSET_THRESHOLD)
                _syslog_flush_file(0);
        }

        g_syslog_ring.head ++;  // 允许溢出
    }
}

void _syslog_entry_push(syslog_module_e module, syslog_facility_e facility, const char *ctx)
{
    pfm_ensure_ret_void(module < SYSLOG_MODULE_MAX && facility < SYSLOG_FACILITY_MAX && ctx);

    syslog_entry_t entry = {
        .module = module,
        .facility = facility,
    };
    // 深拷贝字符串
    strncpy(entry.ctx, ctx, SYSLOG_ENTRY_CTX_SIZE);
    // 生成时间戳
    clock_gettime(CLOCK_REALTIME, &entry.ts);

    // 推送到消息队列
    int ret = msg_q_push(syslog, &entry, sizeof(syslog_entry_t));
    if(msg_q_ret_ok != ret)
        dbg_error("syslog push msg q fail, ret %d", ret);
}

static void* _syslog_show_buffer_cli_hook(unsigned char argc, char *argv[])
{
    // 处理指定数量的参数
    int num = INT32_MAX;
    if(argc == 1)
    {
        num = cli_param_parse_str_2_u32(argv[0]);
        if(num <= 0)
        {
            safe_printf("-syslog: error param %s\n", argv[0]);
            return NULL;
        }
    }

    ev_with_rdlock(&g_syslog_ring.lock)
    {
        int size = _syslog_ring_size();
        if(!size)
            safe_printf("syslog ring buffer is empty.\n");
        else
        {
            safe_printf("syslog buffer cnt: %d\n----------------------\n", size);
            uint32_t head = g_syslog_ring.head;
            uint32_t tail = g_syslog_ring.tail;
            if(num < size)
                head = tail + num;
            uint32_t i = tail;
            for(; i != head; ++ i)
            {
                const syslog_str_t* syslog_str = &g_syslog_ring.entries[syslog_ring_mask(i)];
                safe_printf("%s\n", syslog_str->str);
            }
            safe_printf("----------------------\n");
        }
    }

    return NULL;
}

void syslog_module_init(void)
{
    memset(&g_syslog_ring, 0, sizeof(syslog_ring_t));
    ev_rwlock_init(&g_syslog_ring.lock);    // 初始化读写锁

    // 创建日志消费线程
    pfm_ensure_ret_void(-1 != pthread_create(&syslog_consumer_tid, NULL, _syslog_consumer, NULL));

    // 注册cli读取日志缓存
    cli_param_t param0[1] = {
        {.required = 0, .type = PARAM_VALUE, .short_name = 'n', .help = "dump no more than [n] logs"}
    };
    cli_register("show syslog buffer", "dump syslog ring buffer", param0, _syslog_show_buffer_cli_hook);
    // 注册cli设置日志输出方向
    cli_param_t param1[2] = {
        {.required = 0, .type = PARAM_VALUE, .short_name = 't', .help = "terminal, 0/1"},
        {.required = 0, .type = PARAM_VALUE, .short_name = 'f', .help = "flash, 0/1"}
    };
    cli_register("syslog direction", "set syslog direction", param1, _syslog_direction_cli_hook);
    // 注册cli获取日志输出方向
    cli_register("show syslog direction", "show syslog direction", NULL, _syslog_direction_get_cli_hook);

    // 打开文件
    g_syslog_ring.fd = open(SYSLOG_FLUSH_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if(g_syslog_ring.fd < 0)
        dbg_error("syslog open file %s fail", SYSLOG_FLUSH_PATH);
    // 启动刷盘
    ev_thd_register(syslog_flush);
    ev_thd_run(syslog_flush);

    dbg_major("syslog module init ok");
}