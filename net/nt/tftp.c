/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    tftp.c
 * @brief   tftp实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-27
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-27 | cai | Initial creation, support tftp client.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "nt/tftp.h"
#include "cli/cli.h"
#include "mp/mp_slab.h"
#include "msg_q/msg_q.h"
#include "event/ev_timer.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// tftp协议udp端口号
#define TFTP_UDP_PORT       (69)
// tftp重传次数
#define TFTP_RETRY_COUNT    (3)
// tftp重传间隔
#define TFTP_RETRY_INTERVAL (5)

// TFTP RRQ报文缓冲长度
#define TFTP_RRQ_BUFFER_LEN     (64)

// tftp block size
#define TFTP_DATA_BLOCK_SIZE        (512)

// tftp fsm mq容量
#define TFTP_FSM_MSGQ_CAPACITY      (8)

// 文件名长度
#define FILE_NAME_SIZE              (16)

// TFTP文件名长度
#define TFTP_DOWNLOAD_PATH_SIZE     (256)
// TFTP下载路径前缀
#define TFTP_DOWNLOAD_PATH_PREFIX   "../net/nt/tftp/download/"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// TFTP opcode定义
typedef enum{
    TFTP_RD_REQ = 1,        // 读请求，opcode 2B + filename + '\0' + mode + '\0'
    TFTP_WR_REQ = 2,        // 写请求，opcode 2B + filename + '\0' + mode + '\0'
    TFTP_DATA = 3,          // 数据包，opcode 2B + block_number 2B + data
    TFTP_ACK = 4,           // 确认
    TFTP_ERROR = 5,         // 错误
}tftp_opcode_e;

// TFTP报文格式
typedef struct{
    uint16_t op_code;   // 操作码，见tftp_opcode_e
} attr_packed pkthdr_tftp_t;
_Static_assert(sizeof(pkthdr_tftp_t) == 2, "sizeof pkthdr_tftp_t must be 2");

// TFTP DATA报文格式
typedef struct{
    uint16_t op_code;   // 操作码，见tftp_opcode_e
    uint16_t block_id;      // block id
} attr_packed pkthdr_tftp_data_t;
_Static_assert(sizeof(pkthdr_tftp_data_t) == 4, "sizeof pkthdr_tftp_data_t must be 4");

// TFTP ACK报文格式
typedef struct{
    uint16_t op_code;   // 操作码，见tftp_opcode_e
    uint16_t block_id;      // ack block id
} attr_packed pkthdr_tftp_ack_t;
_Static_assert(sizeof(pkthdr_tftp_ack_t) == 4, "sizeof pkthdr_tftp_ack_t must be 4");

// TFTP状态枚举
typedef enum{
    TFTP_STATE_IDLE = 0,            // 空闲
    TFTP_STATE_WAIT_DATA1,          // 等待第一个block
    TFTP_STATE_DOWNLOAD,            // 剩余block的传输
    TFTP_STATE_SUCCESS,             // 传输完毕状态
    TFTP_STATE_ERROR,               // 错误状态

    TFTP_STATE_CNT,         // 计数
}tftp_state_e;

// TFTP事件枚举，用于驱动状态机
typedef enum{
    TFTP_EVENT_READ_REQ = 0,            // 请求读文件，下载
    TFTP_EVENT_RECEIVE_DATA1,           // 接收到data1
    TFTP_EVENT_RECEIVE_DATA,            // 接收到从2开始的data
    TFTP_EVENT_RECEIVE_DATA_TIMEOUT,    // 读取数据超时
    TFTP_EVENT_RECEIVE_ERROR,           // 收到error packet
    TFTP_EVENT_RE_IDLE,                 // 清除异常，回到idle

    TFTP_EVENT_CNT,                     // 计数
}tftp_event_e;

// TFTP消息格式，用于驱动状态机
typedef struct{
    tftp_event_e event;
    // 以下用于更新会话
    char if_name[IF_NAME_SIZE];         // 接口名
    char file_name[FILE_NAME_SIZE];     // 文件名
    uint32_t server_ip;         // 服务器IP
    uint16_t server_port;       // 服务器端口号
    char data[TFTP_DATA_BLOCK_SIZE];    // 数据内容
    int data_size;                      // 数据长度
}tftp_msg_t;

// 状态机钩子函数定义
typedef void (*tftp_fsm_hook_func)(tftp_msg_t *msg);

// TFTP状态机钩子定义
typedef struct{
    tftp_fsm_hook_func enter_hook;      // 进入状态执行的hook
    tftp_fsm_hook_func poll_hook;       // 计算状态切换的hook
    tftp_fsm_hook_func exit_hook;       // 离开状态执行的hook
}tftp_fsm_hook_t;

// TFTP状态机定义
typedef struct{
    tftp_state_e state;         // 当前状态
    tftp_fsm_hook_t hook[TFTP_STATE_CNT];       // 每个状态对应的钩子函数
    // 以下用于会话
    char if_name[IF_NAME_SIZE];         // 接口名
    char file_name[FILE_NAME_SIZE];     // 文件名
    FILE *fp;                           // 文件指针

    ATOMIC_UINT32_T server_ip;          // 服务器ip
    ATOMIC_UINT16_T server_port;        // 服务器使用端口号，tftp要求动态变更
    ATOMIC_UINT16_T expect_block_id;    // 等待的数据block_id

    uint8_t retry;              // 重试次数
    ev_timer_t *retry_timer;      // 重发报文定时器

    ev_sem_t sem_done;          // 结束的sem，用来通知cli
}tftp_fsm_t;

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// tftp fsm线程
static pthread_t g_tftp_fsm_tid;
// tftp fsm
static tftp_fsm_t g_tftp_fsm = {};

// 声明发包内存池
declare_mem_type_slab_extern(ftx)

// 初始化消息队列，用于状态机线程之间通信
declare_msg_q(tftp_fsm_mq, TFTP_FSM_MSGQ_CAPACITY, sizeof(tftp_msg_t))

// 状态字符串
const char* tftp_state_str[TFTP_STATE_CNT] = {
    "IDLE", "WAIT_DATA1", "DOWNLOAD", "SUCCESS", "TFTP_ERROR",
};
// 事件字符串
const char* tftp_event_str[TFTP_EVENT_CNT] = {
    "READ_REQUEST", "RECEIVE_DATA_1", "RECEIVE_DATE", "DATA_TIMEOUT", "RECEIVE_ERROR_PKT", "RETURN_IDLE", 
};

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       send tftp rrq packet
 * 
 * @note        发送读请求报文
 *              format: op_code(2B) + filename + '\0' + mode + '\0'
 */
static error_code_e _tftp_send_rrq()
{
    int offset = 0;
    const char *mode = "octet";     // 二进制模式

    const char* file_name = g_tftp_fsm.file_name;
    const char* if_name = g_tftp_fsm.if_name;

    if(2 + strlen(file_name)+1 + strlen(mode)+1 > TFTP_RRQ_BUFFER_LEN)      // 检查长度
        return ERR_NO_MEM;

    char *buffer = mp_slab_node_get(ftx, TFTP_RRQ_BUFFER_LEN);
    if(!buffer)
        return ERR_NO_MEM;
    memset(buffer, 0, TFTP_RRQ_BUFFER_LEN);

    pkthdr_tftp_t *tftp_hdr = (pkthdr_tftp_t*)buffer;
    tftp_hdr->op_code = htons(TFTP_RD_REQ);
    offset += 2;

    memcpy(buffer + offset, file_name, strlen(file_name)+1);
    offset += (strlen(file_name)+1);

    memcpy(buffer + offset, mode, strlen(mode)+1);
    offset += (strlen(mode)+1);

    ftx_send_udp(if_name, ATOM_LOAD(&g_tftp_fsm.server_ip, MORDER_ACQUIRE), TFTP_UDP_PORT, buffer, offset);

    return ERR_NO_ERROR;
}

/**
 * @brief       send ack packet
 * 
 * @param[in]   block_id        - ack哪个block
 */
static error_code_e _tftp_send_ack(uint16_t block_id)
{
    pkthdr_tftp_ack_t *ack = mp_slab_node_get(ftx, sizeof(pkthdr_tftp_ack_t));
    if(!ack)
        return ERR_NO_MEM;

    ack->op_code = htons(TFTP_ACK);
    ack->block_id = htons(block_id);

    ftx_send_udp(g_tftp_fsm.if_name, g_tftp_fsm.server_ip, g_tftp_fsm.server_port, ack, sizeof(pkthdr_tftp_ack_t));

    dbg_major("send ack");
}

/**
 * @brief       重传ACK
 * 
 * @note        
 */
static attr_force_inline void _tftp_send_ack_retry(uint16_t block_id)
{
    _tftp_send_ack(block_id);
    safe_printf("-tftp_fsm: Retry send ACK for block #%d (%d/%d)\n", block_id, g_tftp_fsm.retry, TFTP_RETRY_COUNT);
}

/**
 * @brief       download data from tftp_data_packet into file
 * 
 * @param[in]   msg - tftp msg
 */
static error_code_e _tftp_download_data(tftp_msg_t *msg)
{
    if(!g_tftp_fsm.fp)
    {
        safe_printf("-tftp_fsm: file pointer not exist.\n");
        return ERR_NT_TFTP_FILE_FAIL;
    }

    size_t written = fwrite(msg->data, 1, msg->data_size, g_tftp_fsm.fp);
    if(written != msg->data_size)
    {
        safe_printf("-tftp_fsm: fwrite() fail, actually write %d bytes, expected write %d bytes.\n", written, msg->data_size);
        return ERR_NT_TFTP_FILE_FAIL;
    }
    safe_printf("-tftp_fsm: Write block #%d, %d Bytes...\n", ATOM_LOAD(&g_tftp_fsm.expect_block_id, MORDER_ACQUIRE), written);

    return ERR_NO_ERROR;
}

/**
 * @brief       resend tftp rrq packet
 */
static attr_force_inline void _tftp_rrq_retry(void *args)
{
    tftp_msg_t msg = {};

    msg.event = ATOM_LOAD(&g_tftp_fsm.expect_block_id, MORDER_ACQUIRE) == 1 ? 
        TFTP_EVENT_READ_REQ : TFTP_EVENT_RECEIVE_DATA_TIMEOUT;
    msg_q_push(tftp_fsm_mq, &msg, sizeof(tftp_msg_t));
    // 再次启动
    ev_timer_start(g_tftp_fsm.retry_timer);
}

/**
 * @brief       更新fsm会话相关数据
 */
static void _tftp_fsm_session_update(tftp_msg_t *msg)
{
    switch(msg->event)
    {
        case TFTP_EVENT_READ_REQ:
            // 文件名
            memset(g_tftp_fsm.file_name, 0, FILE_NAME_SIZE);
            memcpy(g_tftp_fsm.file_name, msg->file_name, strlen(msg->file_name));
            // 接口名
            memset(g_tftp_fsm.if_name, 0, IF_NAME_SIZE);
            memcpy(g_tftp_fsm.if_name, msg->if_name, strlen(msg->if_name));
            // 服务器IP
            ATOM_STORE(&g_tftp_fsm.server_ip, msg->server_ip, MORDER_RELEASE);
            break;
        case TFTP_EVENT_RECEIVE_DATA1:
            // 更新服务器端口
            ATOM_STORE(&g_tftp_fsm.server_port, msg->server_port, MORDER_RELEASE);
            break;
    }
}

static error_code_e _tftp_open_file(tftp_msg_t *msg)
{
    char filepath[TFTP_DOWNLOAD_PATH_SIZE] = {};

    snprintf(filepath, TFTP_DOWNLOAD_PATH_SIZE, "%s%s", TFTP_DOWNLOAD_PATH_PREFIX, g_tftp_fsm.file_name);
    g_tftp_fsm.fp = fopen(filepath, "wb");
    if(!g_tftp_fsm.fp)
    {
        safe_printf("-tftp_fsm: fopen() file: %s fail\n", filepath);
        return ERR_NT_TFTP_FILE_FAIL;
    }
    return ERR_NO_ERROR;
}

/**
 * @brief       进入idle状态调用
 * 
 * @note        初始化会话信息
 */
static attr_force_inline void _tftp_state_idle_enter(tftp_msg_t *msg)
{
    memset(g_tftp_fsm.if_name, 0, IF_NAME_SIZE);
    memset(g_tftp_fsm.file_name, 0, FILE_NAME_SIZE);
    ATOM_STORE(&g_tftp_fsm.server_ip, 0, MORDER_RELEASE);
    ATOM_STORE(&g_tftp_fsm.server_port, 0, MORDER_RELEASE);
    ATOM_STORE(&g_tftp_fsm.expect_block_id, 0, MORDER_RELEASE);
    g_tftp_fsm.retry = 0;

    // 通知cli完毕
    ev_sem_post(&g_tftp_fsm.sem_done);
}

/**
 * @brief       idle状态切换判断
 */
static attr_force_inline void _tftp_state_idle_poll(tftp_msg_t *msg)
{
    tftp_event_e event = msg->event;

    switch(event)
    {
        case TFTP_EVENT_READ_REQ:       // 请求下载
            // 更新会话信息
            _tftp_fsm_session_update(msg);
            g_tftp_fsm.state = TFTP_STATE_WAIT_DATA1;
        default:
            break;
    }
}

/**
 * @brief       进入wait_data1状态调用
 * 
 * @note        发送RRQ报文，启动重传定时器
 */
static attr_force_inline void _tftp_state_wait_data1_enter(tftp_msg_t *msg)
{
    ATOM_STORE(&g_tftp_fsm.expect_block_id, 1, MORDER_RELEASE);

    _tftp_send_rrq();
    safe_printf("-tftp_fsm: Send RRQ Packet\n");
    ev_timer_start(g_tftp_fsm.retry_timer);
}

/**
 * @brief       wait_data1状态切换判断
 */
static attr_force_inline void _tftp_state_wait_data1_poll(tftp_msg_t *msg)
{
    tftp_event_e event = msg->event;

    switch(event)
    {
        case TFTP_EVENT_READ_REQ:
            if(g_tftp_fsm.retry < TFTP_RETRY_COUNT)     // 有限次数超时重传
            {
                _tftp_send_rrq();
                ++ g_tftp_fsm.retry;
                safe_printf("-tftp_fsm: Retry send RRQ (%d/%d)\n", g_tftp_fsm.retry, TFTP_RETRY_COUNT);
            }
            else
            {
                g_tftp_fsm.retry = 0;
                g_tftp_fsm.state = TFTP_STATE_ERROR;
            }
            break;
        case TFTP_EVENT_RECEIVE_DATA1:                  // 接收到DATA1
            safe_printf("-tftp_fsm: Receive Data1 From Server.\n");
            int ret = _tftp_open_file(msg);             // 打开文件
            if(ERR_NO_ERROR != ret)
                g_tftp_fsm.state = TFTP_STATE_ERROR;
            else
            {
                // 下载block1 data
                int ret = _tftp_download_data(msg);
                if(ERR_NO_ERROR != ret)
                    g_tftp_fsm.state = TFTP_STATE_ERROR;
                else
                {
                    // 更新会话数据，服务器端口用于回复
                    _tftp_fsm_session_update(msg);
                    // 回复第一个block的ACK
                    ATOM_FETCH_ADD(&g_tftp_fsm.expect_block_id, 1, MORDER_ACQ_REL); // 等待下一个block
                    _tftp_send_ack(1);
                    if(msg->data_size == TFTP_DATA_BLOCK_SIZE)
                        g_tftp_fsm.state = TFTP_STATE_DOWNLOAD;     // 进入transfer状态
                    else
                        g_tftp_fsm.state = TFTP_STATE_SUCCESS;      // 传输完毕，进入success
                }
            }
            break;
        case TFTP_EVENT_RECEIVE_ERROR:
            g_tftp_fsm.state = TFTP_STATE_ERROR;
            safe_printf("-tftp_fsm: Receive Error From Server.\n");
            break;
        default:
            break;
    }
}

/**
 * @brief       离开wait_data1状态调用
 * 
 * @note        停止rrq重传定时器
 */
static attr_force_inline void _tftp_state_wait_data1_exit(tftp_msg_t *msg)
{
    g_tftp_fsm.retry = 0;       // 重置重传次数
    ev_timer_stop(g_tftp_fsm.retry_timer);    // 结束定时器
}

/**
 * @brief       进入transfer状态调用
 * 
 * @note        
 */
static attr_force_inline void _tftp_state_download_enter(tftp_msg_t *msg)
{
    // 启动ACK定时器
    ev_timer_start(g_tftp_fsm.retry_timer);
}

/**
 * @brief       transfer状态切换
 */
static attr_force_inline void _tftp_state_download_poll(tftp_msg_t *msg)
{
    tftp_event_e event = msg->event;

    switch(event)
    {
        case TFTP_EVENT_RECEIVE_DATA:       // 接收到数据
            // 停止ACK定时器
            ev_timer_stop(g_tftp_fsm.retry_timer);
            g_tftp_fsm.retry = 0;
            // 下载该block数据
            int ret = _tftp_download_data(msg);
            if(ret == ERR_NO_ERROR)
            {
                // 发送ACK
                uint16_t block_id =  ATOM_LOAD(&g_tftp_fsm.expect_block_id, MORDER_ACQUIRE);
                ATOM_FETCH_ADD(&g_tftp_fsm.expect_block_id, 1, MORDER_ACQ_REL);         // 等待下一个block
                _tftp_send_ack(block_id);
                // 判断是否最后一个block，进入不同分支
                if(msg->data_size < TFTP_DATA_BLOCK_SIZE)
                    g_tftp_fsm.state = TFTP_STATE_SUCCESS;
                else
                {
                    g_tftp_fsm.state = TFTP_STATE_DOWNLOAD;
                    // 重启ACK定时器
                    ev_timer_start(g_tftp_fsm.retry_timer);
                }
            }
            else
                g_tftp_fsm.state = TFTP_STATE_ERROR;
            break;
        case TFTP_EVENT_RECEIVE_DATA_TIMEOUT:   // 超时
            if(g_tftp_fsm.retry < TFTP_RETRY_COUNT)
            {
                g_tftp_fsm.retry ++;
                // 重发上一个block的ACK
                _tftp_send_ack_retry(ATOM_LOAD(&g_tftp_fsm.expect_block_id, MORDER_ACQUIRE) - 1);
            }
            else
                g_tftp_fsm.state = TFTP_STATE_ERROR;
            break;
        case TFTP_EVENT_RECEIVE_ERROR:
            // 停止ACK定时器
            ev_timer_stop(g_tftp_fsm.retry_timer);
            g_tftp_fsm.retry = 0;
            g_tftp_fsm.state = TFTP_STATE_ERROR;
            safe_printf("-tftp_fsm: Receive Error From Server.\n");
            break;
        default:
            break;
    }
}

/**
 * @brief       进入success状态调用
 * 
 * @note        自写RE_IDLE事件，回到idle
 */
static attr_force_inline void _tftp_state_success_enter(tftp_msg_t *msg)
{
    safe_printf("-tftp_fsm: Download file done.\n");

    tftp_msg_t self_msg = {
        .event = TFTP_EVENT_RE_IDLE,
    };
    msg_q_push(tftp_fsm_mq, &self_msg, sizeof(tftp_msg_t));
}

/**
 * @brief       success状态切换
 */
static attr_force_inline void _tftp_state_success_poll(tftp_msg_t *msg)
{
    tftp_event_e event = msg->event;

    switch(event)
    {
        case TFTP_EVENT_RE_IDLE:
            g_tftp_fsm.state = TFTP_STATE_IDLE;
            break;
        default:
            break;
    }
}

/**
 * @brief       离开success状态调用
 * 
 * @note        关闭文件
 */
static attr_force_inline void _tftp_state_success_exit(tftp_msg_t *msg)
{
    fflush(g_tftp_fsm.fp);
    fclose(g_tftp_fsm.fp);
    g_tftp_fsm.fp = NULL;
}

/**
 * @brief       进入error状态调用
 * 
 * @note        设置re idle msg，自驱动回到idle
 */
static attr_force_inline void _tftp_state_error_enter(tftp_msg_t *msg)
{
    tftp_msg_t self_msg = {
        .event = TFTP_EVENT_RE_IDLE,
    };
    msg_q_push(tftp_fsm_mq, &self_msg, sizeof(tftp_msg_t));
}

/**
 * @brief       error状态切换
 */
static attr_force_inline void _tftp_state_error_poll(tftp_msg_t *msg)
{
    tftp_event_e event = msg->event;

    switch(event)
    {
        case TFTP_EVENT_RE_IDLE:
            g_tftp_fsm.state = TFTP_STATE_IDLE;
            break;
        default:
            break;
    }
}

/**
 * @brief       error状态离开
 */
static attr_force_inline void _tftp_state_error_exit(tftp_msg_t *msg)
{
    // 关闭文件描述符
    if(g_tftp_fsm.fp)
    {
        fclose(g_tftp_fsm.fp);
        g_tftp_fsm.fp = NULL;
    }
}

/**
 * @brief       clear g_tftp_session
 */
static attr_force_inline void _tftp_fsm_init()
{
    // 初始化fsm信息
    memset(&g_tftp_fsm, 0, sizeof(g_tftp_fsm));
    g_tftp_fsm.state = TFTP_STATE_IDLE;
    g_tftp_fsm.retry_timer = ev_timer_create(TFTP_RETRY_INTERVAL*1000, _tftp_rrq_retry, NULL);
    ev_sem_init(&g_tftp_fsm.sem_done);

    // IDLE
    g_tftp_fsm.hook[TFTP_STATE_IDLE].enter_hook = _tftp_state_idle_enter;
    g_tftp_fsm.hook[TFTP_STATE_IDLE].poll_hook = _tftp_state_idle_poll;
    g_tftp_fsm.hook[TFTP_STATE_IDLE].exit_hook = NULL;
    // WAIT_DTAT1
    g_tftp_fsm.hook[TFTP_STATE_WAIT_DATA1].enter_hook = _tftp_state_wait_data1_enter;
    g_tftp_fsm.hook[TFTP_STATE_WAIT_DATA1].poll_hook = _tftp_state_wait_data1_poll;
    g_tftp_fsm.hook[TFTP_STATE_WAIT_DATA1].exit_hook = _tftp_state_wait_data1_exit;
    // TRANSFER
    g_tftp_fsm.hook[TFTP_STATE_DOWNLOAD].enter_hook = _tftp_state_download_enter;
    g_tftp_fsm.hook[TFTP_STATE_DOWNLOAD].poll_hook = _tftp_state_download_poll;
    g_tftp_fsm.hook[TFTP_STATE_DOWNLOAD].exit_hook = NULL;
    // SUCCESS
    g_tftp_fsm.hook[TFTP_STATE_SUCCESS].enter_hook = _tftp_state_success_enter;
    g_tftp_fsm.hook[TFTP_STATE_SUCCESS].poll_hook = _tftp_state_success_poll;
    g_tftp_fsm.hook[TFTP_STATE_SUCCESS].exit_hook = _tftp_state_success_exit;
    // ERROR
    g_tftp_fsm.hook[TFTP_STATE_ERROR].enter_hook = _tftp_state_error_enter;
    g_tftp_fsm.hook[TFTP_STATE_ERROR].poll_hook = _tftp_state_error_poll;
    g_tftp_fsm.hook[TFTP_STATE_ERROR].exit_hook = NULL;
}

/**
 * @brief       tftp fsm routine
 * 
 * @brief       tftp状态机线程入口函数
 */
static void* _tftp_fsm(void *args);

/**
 * @brief       cli hook for tftp download
 * 
 * @param[in]   if_name     - if name
 * @param[in]   server_ip   - host order
 * @param[in]   filename    - file name
 */
static void* _tftp_download_cli_hook(unsigned char argc, char *argv[]);

/**
 * @brief       cb for zcap filter tftp packet
 * 
 * @param[in]   packet
 * @param[in]   len
 * @param[in]   args        - user args
 * 
 * @note        udp抓包回调，TFTP协议端口号不是固定69，会动态选择，需要自行过滤报文
 */
static void _tftp_packet_filter_cb(char *packet, int len, void *args);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static void* _tftp_download_cli_hook(unsigned char argc, char *argv[])
{
    // 检查接口长度
    if(strlen(argv[0]) >= IF_NAME_SIZE)
    {
        safe_printf("-tftp: Length of ifname too long\n");
        return NULL;
    }
    // 检查接口状态
    if(!(zcap_status_get(argv[0]) && ftx_status_get(argv[0])))
    {
        safe_printf("-tftp: Interface invalid: %s\n", argv[0]);
        return NULL;
    }

    // 检查文件长度
    if(strlen(argv[2]) >= FILE_NAME_SIZE)
    {
        safe_printf("-tftp: Length of filename too long\n");
        return NULL;
    }

    // 获取server ip
    const char *server_ip_str = argv[1];
    struct in_addr addr = {};
    if(1 != inet_pton(AF_INET, server_ip_str, &addr))
    {
        safe_printf("-tftp: Error Server IP: %s\n", server_ip_str);
        return NULL;
    }
    uint32_t server_ip_host = ntohl(addr.s_addr);

    tftp_msg_t msg = {
        .event = TFTP_EVENT_READ_REQ,
        .server_ip = server_ip_host,
    };
    memcpy(msg.if_name, argv[0], strlen(argv[0]));
    memcpy(msg.file_name, argv[2], strlen(argv[2]));
    msg_q_push(tftp_fsm_mq, &msg, sizeof(tftp_msg_t));

    // 等待完成
    ev_sem_wait(&g_tftp_fsm.sem_done);

    return NULL;
}

static void _tftp_packet_filter_cb(char *packet, int len, void *args)
{
    // 检查报文长度是否小于tftp的
    if(len < sizeof(pkthdr_l2_t) + sizeof(pkthdr_ipv4_t) + sizeof(pkthdr_udp_t) + sizeof(pkthdr_tftp_t))
        return;

    // 检查源IP是否来自tftp服务器
    pkthdr_ipv4_t *ipv4_hdr = (pkthdr_ipv4_t*)(packet + sizeof(pkthdr_l2_t));
    if(ntohl(ipv4_hdr->src_addr) != ATOM_LOAD(&g_tftp_fsm.server_ip, MORDER_ACQUIRE))
        return;
    
    // 检查TFTP op_code是否为TFTP_DATA，或者ERROR
    pkthdr_tftp_t *tftp_hdr = (pkthdr_tftp_t*)(packet + sizeof(pkthdr_l2_t) + sizeof(pkthdr_ipv4_t) + sizeof(pkthdr_udp_t));
    uint16_t op_code = ntohs(tftp_hdr->op_code);
    tftp_msg_t msg = {};
    if(op_code == TFTP_ERROR)
    {
        msg.event = TFTP_EVENT_RECEIVE_ERROR;
        msg_q_push(tftp_fsm_mq, &msg, sizeof(msg));
    }
    if(op_code != TFTP_DATA)
        return;

    pkthdr_tftp_data_t *tftp_data_hdr = (pkthdr_tftp_data_t*)tftp_hdr;
    uint16_t block_id = ntohs(tftp_data_hdr->block_id);

    if(ATOM_LOAD(&g_tftp_fsm.expect_block_id, MORDER_ACQUIRE) != block_id)
        return;

    char *data = (char*)tftp_data_hdr + sizeof(pkthdr_tftp_data_t);
    int data_size = len - (sizeof(pkthdr_l2_t) + sizeof(pkthdr_ipv4_t) + sizeof(pkthdr_udp_t) + sizeof(pkthdr_tftp_data_t));
    // 如果block_id为1，推送RECEIVE_DATA1事件
    if(block_id == 1)
    {
        msg.event = TFTP_EVENT_RECEIVE_DATA1;
        pkthdr_udp_t *udp_hdr = (pkthdr_udp_t*)(packet + sizeof(pkthdr_l2_t) + sizeof(pkthdr_ipv4_t));
        msg.server_port = ntohs(udp_hdr->sport);
        memcpy(msg.data, data, data_size);
        msg.data_size = data_size;
        msg_q_push(tftp_fsm_mq, &msg, sizeof(msg));
    }
    else        // 否则是RECEIVE_DATA事件
    {
        msg.event = TFTP_EVENT_RECEIVE_DATA;
        memcpy(msg.data, data, data_size);
        msg.data_size = data_size;
        msg_q_push(tftp_fsm_mq, &msg, sizeof(msg));
    }
}

static void* _tftp_fsm(void *args)
{
    // 状态机初始化，设置hook等
    _tftp_fsm_init();

    tftp_msg_t msg = {};
    int ret = 0;
    tftp_state_e state = 0;
    tftp_event_e event = 0;
    while(1)
    {
        // 阻塞等待外部消息
        msg_q_pop(tftp_fsm_mq, sizeof(tftp_msg_t), msg_q_wait_forever, &msg);

        state = g_tftp_fsm.state;
        event = msg.event;
        dbg_major("tftp session state: %s, event %s", tftp_state_str[state], tftp_event_str[event]);

        // 运行状态切换钩子函数
        g_tftp_fsm.hook[state].poll_hook(&msg);

        // 如果状态变换，执行exit和enter钩子
        tftp_state_e new_state = g_tftp_fsm.state;
        dbg_major("tftp state change to: %s", tftp_state_str[new_state]);
        if(new_state != state)
        {
            if(g_tftp_fsm.hook[state].exit_hook)            // 离开state
                g_tftp_fsm.hook[state].exit_hook(&msg);
            if(g_tftp_fsm.hook[new_state].enter_hook)       // 进入new_state
                g_tftp_fsm.hook[new_state].enter_hook(&msg);
        }
    }
}

void tftp_module_init()
{
    // 注册cli，从tftp服务器下载文件
    cli_param_t param0[] = {
        {.required = 1, .type = PARAM_VALUE, .short_name = 'i', .help = "interface name"},
        {.required = 1, .type = PARAM_VALUE, .short_name = 's', .help = "tftp server ip addr"},
        {.required = 1, .type = PARAM_VALUE, .short_name = 'f', .help = "filename"}
    };
    cli_register("tftp download", "download file from server", param0, _tftp_download_cli_hook);

    // 注册UDP报文过滤，TFTP传输数据会指定随机的端口号，这里只能粗匹配
    zcap_register_pkt_filter("eth0", "udp", _tftp_packet_filter_cb, NULL);
    zcap_register_field_len_type("eth0", "udp", IPV4, 0xffff);
    zcap_register_field_pro_type("eth0", "udp", UDP, 0xff);

    zcap_register_pkt_filter("wlan0", "udp", _tftp_packet_filter_cb, NULL);
    zcap_register_field_len_type("wlan0", "udp", IPV4, 0xffff);
    zcap_register_field_pro_type("wlan0", "udp", UDP, 0xff);

    // 启用fsm线程
    int ret = pthread_create(&g_tftp_fsm_tid, NULL, _tftp_fsm, NULL);
    if(0 != ret)
        dbg_error("tftp fsm init fail");

    dbg_major("tftp module init ok");
}