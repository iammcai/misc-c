/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    ev_thread.h
 * @brief   事件线程头文件
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-05-29
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-05-29 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include "nt/tftp.h"
#include "cli/cli.h"
#include "mp/mp_slab.h"

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// TFTP opcode定义
typedef enum{
    TFTP_RD_REQ = 1,        // 读请求，opcode 2B + filename + '\0' + mode + '\0'
    TFTP_WR_REQ = 2,        // 写请求
    TFTP_DATA = 3,          // 数据包，opcode 2B + block_number 2B + data
    TFTP_ACK = 4,           // 确认
    TFTP_ERROR = 5,         // 错误
}tftp_opcode_e;

// TFTP报文格式
typedef struct{
    uint16_t op_code;   // 操作码，见tftp_opcode_e
} attr_packed pkthdr_tftp_t;
_Static_assert(sizeof(pkthdr_tftp_t) == 2, "sizeof pkthdr_tftp_t must be 2");

// TFTP会话状态枚举
typedef enum{
    TFTP_SESSION_IDLE = 0,          // 空闲
    TFTP_SESSION_WAIT_DATA,         // 等待数据
}tftp_session_state_e;

// TFTP会话结构定义
typedef struct{
    ATOMIC_UINT8_T state;       // 会话状态
    ATOMIC_UINT32_T server_ip;  // 服务器IP
    ATOMIC_UINT16_T block_id;   // 等待的blockID
}tftp_session_t;

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// tftp协议udp端口号
#define TFTP_UDP_PORT       (69)
// tftp重传次数
#define TFTP_RETRY_COUNT    (5)
// tftp重传间隔
#define TFTP_RETRY_INTERVAL (5)

// RRQ报文预留缓冲长度
#define TFTP_RRQ_BUFFER_LEN (64)

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 全局tftp session
static tftp_session_t g_tftp_sesssion = {};
// 用于session的mutex
static pthread_mutex_t g_tftp_session_mtx = PTHREAD_MUTEX_INITIALIZER;
// 用于session的cond
static pthread_cond_t g_tftp_session_cond;

// 声明发包内存池
declare_mem_type_slab_extern(ftx)

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       build tftp read request packet
 * 
 * @param[in]   filename    - file name
 * 
 * @param[out]  tftp_payload    - payload of tftp packet
 * 
 * @retval      offset  - length of payload, 0 - fail
 * 
 * @note        
 */
static int _fttp_rd_req_build(const char *filename, int buf_len, char *tftp_payload)
{
    assert(filename && tftp_payload);

    int offset = 0;
    const char *mode = "octet"; // 二进制模式
    // 检查缓冲长度是否足够
    if(2 + strlen(filename)+1 + strlen(mode)+1 > buf_len)
    {
        dbg_error("Buffer len not enough");
        return 0;
    }

    // opcode
    pkthdr_tftp_t *tftp_hdr = (pkthdr_tftp_t*)tftp_payload;
    tftp_hdr->op_code = htons(TFTP_RD_REQ);
    offset += 2;
    // filename
    memcpy(tftp_payload + offset, filename, strlen(filename)+1);
    offset += (strlen(filename)+1);
    // mode
    memcpy(tftp_payload + offset, mode, strlen(mode)+1);
    offset += (strlen(mode)+1);

    return offset;
}

/**
 * @brief       send tftp rrq, wait first data come
 * 
 * @param[in]   if_name     - interface name
 * @param[in]   server_ip   - server ip, host order
 * @param[in]   filename    - file name
 * 
 * @retval      error code
 * 
 * @note        发送RRQ，超时重传，等待接收到第一个data block
 */
static error_code_e _tftp_send_rrq_wait(const char *if_name, uint32_t server_ip, const char *filename);

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
 * @note        tftp抓包回调
 */
static void _tftp_packet_filter_cb(char *packet, int len, void *args);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static error_code_e _tftp_send_rrq_wait(const char *if_name, uint32_t server_ip, const char *filename)
{
    pfm_ensure_ret(if_name && server_ip, ERR_BAD_PARAM);
    int retry = 0;      // 重传次数

    // 构造rrq报文负载
    char *payload = mp_slab_node_get(ftx, TFTP_RRQ_BUFFER_LEN);
    memset(payload, 0, TFTP_RRQ_BUFFER_LEN);
    int payload_size = 0;       // 实际负载长度
    if(0 == (payload_size = _fttp_rd_req_build(filename, TFTP_RRQ_BUFFER_LEN, payload)))
    {
        mp_slab_node_put(payload);
        return ERR_BAD_PARAM;
    }

    pthread_mutex_lock(&g_tftp_session_mtx);
    {
        // 设置session状态为WAITDATA，填充server_ip、block
        g_tftp_sesssion.state = TFTP_SESSION_WAIT_DATA;
        g_tftp_sesssion.server_ip = server_ip;
        g_tftp_sesssion.block_id = 1;

        // 发送第一个RRQ请求
        ftx_send_udp(if_name, server_ip, TFTP_UDP_PORT, payload, payload_size);
        safe_printf("-tftp: Send TFTP RRQ done, waiting data...\n");

        // 获取超时绝对时间
        struct timespec ts = {};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_sec += TFTP_RETRY_INTERVAL;           // 协议规定5s
        // 等待state变化，让出锁
        while(g_tftp_sesssion.state == TFTP_SESSION_WAIT_DATA)
        {
            int ret = pthread_cond_timedwait(&g_tftp_session_cond, &g_tftp_session_mtx, &ts);
            if(ret == ETIMEDOUT && retry < TFTP_RETRY_COUNT)    // 判断是否超时，需要重传
            {
                
                
            }
        }
    }
    pthread_mutex_unlock(&g_tftp_session_mtx);

    return ERR_NO_ERROR;
}

static void* _tftp_download_cli_hook(unsigned char argc, char *argv[])
{
    // 获取接口
    const char *if_name = argv[0];
    // 获取server ip
    const char *server_ip_str = argv[1];
    struct in_addr addr = {};
    if(1 != inet_pton(AF_INET, server_ip_str, &addr))
    {
        safe_printf("-tftp: Error Server IP: %s\n", server_ip_str);
        return NULL;
    }
    uint32_t server_ip_host = ntohl(addr.s_addr);
    // 获取文件名
    const char *filename = argv[2];

    

    return NULL;
}

static void _tftp_packet_filter_cb(char *packet, int len, void *args)
{
    //dbg_always("receive udp");
}

void tftp_module_init()
{
    // 设置条件变量，绑定到MONO时钟而不是默认REALTIME
    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC); 
    pthread_cond_init(&g_tftp_session_cond, &cattr);
    pthread_condattr_destroy(&cattr);

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

    dbg_major("tftp module init ok");
}