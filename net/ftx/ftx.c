/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    zctx.c
 * @brief   零拷贝发包实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-07-08
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-07-08 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#define _GNU_SOURCE         // for mmsg
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>     // for socket
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include "ftx/ftx.h"
#include "mp/mp.h"
#include "msg_q/msg_q.h"
#include "cli/cli.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 发包缓冲区大小，4MB
#define FTX_TX_BUFFER_SIZE  (4 << 20)

// 发包消息队列长度
#define FTX_TX_MSGQ_SIZE    (64)

// 报文长度
#define FTX_FRAME_SIZE      (1600)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 报文信息
typedef struct{
    char *packet;       // 报文内容，需要动态申请
    uint16_t len;       // 有效长度
}ftx_packet_t;

// tx报文统计信息
typedef struct{
    ATOMIC_UINT64_T tx_count;       // 主动发送数量
    ATOMIC_UINT64_T tx_size;        // 主动发送大小，单位B
}ftx_stats_t;

// ftx管理结构定义
struct ftx_s{
    const char *if_name;    // 接口名
    int if_index;           // 接口index
    uint8_t if_mac[MAC_ADDR_SIZE];  // 接口mac
    struct in_addr ipv4_addr;       // 接口ip，网络字节序
    char ipv4_str[INET_ADDRSTRLEN];   // ip字符串

    int raw_fd;             // SOCKET_RAW 使用的fd
    struct sockaddr_ll dest;    // 缓存目的

    msg_q_t *tx_q;          // 消息队列，存储发包信息
    pthread_t tx_tid;       // 发包线程
    ftx_stats_t stat;       // 统计数据

    unsigned char status;   // 工作状态

    ftx_hash_item_t item;   // item
};

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 全局哈希表，管理所有ftxor
static ftx_hash_head_t g_ftx_hash = {};

// 声明发包使用的nonfixed attr
declare_mem_type_nonfixed(ftx)

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       cmp two ftx_t
 * 
 * @param[in]   f1  - ftx 1
 * @param[in]   f2  - ftx 2
 * 
 * @retval      cmp result
 * 
 * @note        cmp by ifname
 */
static attr_pure_inline int _ftx_cmp(ftx_t *f1, ftx_t *f2)
{
    assert(f1 && f2 && f1->if_name && f2->if_name);
    return strcmp(f1->if_name, f2->if_name);
}

/**
 * @brief       hash ftxor
 * 
 * @param[in]   f   - ftxor
 * 
 * @retval      hash val
 * 
 * @note        hash by ifname
 */
static attr_pure_inline unsigned int _ftx_hash(ftx_t *f)
{
    assert(f && f->if_name);
    return type_hash_jhash(f->if_name, strlen(f->if_name), 0);
}

// 定义ftx哈希表操作
declare_hash(ftx, ftx, ftx_t, item, 2, 31, _ftx_cmp, _ftx_hash);

/**
 * @brief       clean up ftxor
 * 
 * @param[in]   ftxor   - ftx or
 */
static attr_force_inline void _ftx_cleanup(ftx_t *ftxor)
{
    // 关闭socket
    if(ftxor->raw_fd)
    {
        close(ftxor->raw_fd);
        ftxor->raw_fd = -1;
    }

    // 释放内存
    mp_free((char*)ftxor->if_name, strlen(ftxor->if_name));
    mp_free(ftxor, sizeof(ftx_t));
}

/**
 * @brief       tx routine
 * 
 * @param[in]   ftxor
 * 
 * @note        监听消息队列，批量发包
 */
static void* _ftx_tx_routine(void *args);

/**
 * @brief       cli hook for show ftxor
 */
static void* _ftxor_dump_cli_hook(unsigned char argc, char* agrv[])
{
    ftx_t *ftxor = ftx_hash_first(&g_ftx_hash);

    while(ftxor)
    {
        safe_printf(
            "\n********************************\nif_name: %s\nif_index: %d\nif_mac: %02x-%02x-%02x-%02x-%02x-%02x\n"
            "if_ipv4: %s\n"
            "status: %s\n\ntx_count: %lu\ntx_size: %lu Bytes\n", 
            ftxor->if_name, ftxor->if_index,
            ftxor->if_mac[0],ftxor->if_mac[1],ftxor->if_mac[2],
            ftxor->if_mac[3],ftxor->if_mac[4],ftxor->if_mac[5],
            ftxor->ipv4_str,
            ATOM_LOAD(&ftxor->status, MORDER_ACQUIRE) ? "ready" : "invalid",
            ATOM_LOAD(&ftxor->stat.tx_count, MORDER_ACQUIRE),
            ATOM_LOAD(&ftxor->stat.tx_size, MORDER_ACQUIRE)
        );

        safe_printf("********************************\n\n");
        
        ftxor = ftx_hash_next(&g_ftx_hash, ftxor);
    }

    return NULL;
}

/**
 * @brief       ctor init ftx module
 */
static void ftx_early_init(void) attr_ctor(CTOR_PRIO_MID);

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void _ftx_init(const char *if_name)
{
    // 检查是否已存在
    ftx_t key = {.if_name = if_name};
    ftx_t *result = ftx_hash_find(&g_ftx_hash, &key);
    if(result)
        return;

    ftx_t *ftxor = mp_calloc(1, sizeof(ftx_t));
    ftxor->if_name = mp_strdup(if_name);    // 动态申请字符串空间
    ftxor->raw_fd = -1;
    ftxor->if_index = -1;

    // 创建socket
    ftxor->raw_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if(ftxor->raw_fd < 0)
    {
        dbg_error("create raw socket fail");
        goto cleanup;
    }

    // 获取接口索引并绑定
    struct ifreq ifr = {};
    strncpy(ifr.ifr_name, ftxor->if_name, IF_NAMESIZE-1);
    if(ioctl(ftxor->raw_fd, SIOCGIFINDEX, &ifr) < 0)
    {
        dbg_error("get if index fail");
        goto cleanup;
    }
    ftxor->if_index = ifr.ifr_ifindex;
    // 获取接口mac
    if(-1 == ioctl(ftxor->raw_fd, SIOCGIFHWADDR, &ifr))
    {
        dbg_error("ioctl get hwaddr fail");
        goto cleanup;
    }
    memcpy(ftxor->if_mac, ifr.ifr_hwaddr.sa_data, MAC_ADDR_SIZE);
    // 获取接口ip
    if(0 == ioctl(ftxor->raw_fd, SIOCGIFADDR, &ifr))
    {
        struct sockaddr_in *ip_addr = (struct sockaddr_in *)&ifr.ifr_addr;
        ftxor->ipv4_addr = ip_addr->sin_addr;   // 保存网络字节序
        inet_ntop(AF_INET, &ip_addr->sin_addr, ftxor->ipv4_str, INET_ADDRSTRLEN);
    }
    else
        dbg_error("ioctl get ip addr fail, err: %s", strerror(errno));

    // 设置发包目的
    ftxor->dest.sll_family = AF_PACKET;
    ftxor->dest.sll_ifindex = ftxor->if_index;  // 设置发包接口ifindex
    ftxor->dest.sll_protocol = htons(ETH_P_ALL);
    ftxor->dest.sll_halen = ETH_ALEN;       // 设置mac长度

    // 设置缓冲区大小，失败报警
    int txbuf_size = FTX_TX_BUFFER_SIZE;
    if(setsockopt(ftxor->raw_fd, SOL_SOCKET, SO_SNDBUF, &txbuf_size, sizeof(txbuf_size)) < 0)
        dbg_error("set tx buffer size fail");

    // 设置标志
    ATOM_STORE(&ftxor->status, 1, MORDER_RELEASE);

    // 加入全局哈希表
    ftx_hash_add(&g_ftx_hash, ftxor);
    // 创建消息队列
    ftxor->tx_q = msg_q_create(ftxor->if_name, FTX_TX_MSGQ_SIZE, sizeof(ftx_packet_t));
    // 创建发包线程
    pthread_create(&ftxor->tx_tid, NULL, _ftx_tx_routine, ftxor);

    dbg_major("ftxor on interface %s init ok", ftxor->if_name);
    return;

cleanup:
    _ftx_cleanup(ftxor);
}

static void* _ftx_tx_routine(void *args)
{
    ftx_t *ftxor = (ftx_t*)args;
    ftx_packet_t packet_batch[FTX_TX_MSGQ_SIZE] = {};   // 待发送报文信息
    struct mmsghdr mmsg[FTX_TX_MSGQ_SIZE] = {};         // 内核消息
    struct iovec iov[FTX_TX_MSGQ_SIZE] = {};
    int pendings = 0;   // 待发送数量

    // 初始填充消息
    int i = 0;
    for(; i < FTX_TX_MSGQ_SIZE; ++ i)
    {
        mmsg[i].msg_hdr.msg_name = &ftxor->dest;
        mmsg[i].msg_hdr.msg_namelen = sizeof(ftxor->dest);
        mmsg[i].msg_hdr.msg_iov = &iov[i];
        mmsg[i].msg_hdr.msg_iovlen = 1;
    }

    while(ATOM_LOAD(&ftxor->status, MORDER_ACQUIRE))
    {
        // 阻塞等待消息
        pendings = 0;
        _msg_q_pop(ftxor->tx_q, sizeof(ftx_packet_t), msg_q_wait_forever, &packet_batch[pendings]);
        iov[pendings].iov_base = packet_batch[pendings].packet;
        iov[pendings].iov_len = packet_batch[pendings].len;

        // 非阻塞取完所有剩余信息，最多一次拿64个
        pendings ++;
        while(pendings < FTX_TX_MSGQ_SIZE && msg_q_ret_ok == _msg_q_pop(ftxor->tx_q, sizeof(ftx_packet_t), msg_q_no_wait, &packet_batch[pendings]))
        {
            iov[pendings].iov_base = packet_batch[pendings].packet; // 设置报文内容
            iov[pendings].iov_len = packet_batch[pendings].len;     // 更新长度
            ++ pendings;
        }

        // 通过sendmmsg批量发送
        int remains = pendings;
        struct mmsghdr *ptr = mmsg;
        ftx_packet_t *pkt_ptr = packet_batch;
        while(remains > 0)
        {
            int send = sendmmsg(ftxor->raw_fd, ptr, remains, MSG_DONTWAIT);
            if(send < 0)
            {
                if(errno == EAGAIN || errno == EWOULDBLOCK)     // 等待一下
                {
                    usleep(50); // sleep 50us重新尝试
                    continue;
                }
                else        // 出现异常
                {
                    dbg_error("sendmmsg error occur");
                    break;
                }
            }

            dbg_major("send %d packet ok", send);

            // 统计信息更新
            ATOM_FETCH_ADD(&ftxor->stat.tx_count, send, MORDER_ACQ_REL);
            // 统计数据，释放内存
            int i = 0;
            uint64_t size = 0;
            for(; i < send; ++ i)
            {
                size += pkt_ptr[i].len;
                mp_nonfixed_node_put(pkt_ptr[i].packet);
            }
            ATOM_FETCH_ADD(&ftxor->stat.tx_size, size, MORDER_ACQ_REL);

            pkt_ptr += send;
            ptr += send;
            remains -= send;
        }
    }

    // TODO: 退出，需要回收资源

    return NULL;
}

void _ftx_send(const char *if_name, void *ctx, unsigned int len)
{
    assert(if_name && ctx);

    // 获取接口
    ftx_t key = {.if_name = if_name};
    ftx_t *ftxor = ftx_hash_find(&g_ftx_hash, &key);
    if(!ftxor)
    {
        mp_nonfixed_node_put(ctx);  // 释放内存
        return;
    }

    ftx_packet_t packet = {
        .packet = ctx, 
        .len = len
    };

    // 推送到消息队列中
    while(ATOM_LOAD(&ftxor->status, MORDER_ACQUIRE))
        if(msg_q_ret_ok == _msg_q_push(ftxor->tx_q, &packet, sizeof(packet)))
            break;

    dbg_major("push msg to send packet ok");
}

error_code_e ftx_mac_get(const char *if_name, uint8_t *mac_addr)
{
    pfm_ensure_ret(mac_addr, ERR_BAD_PARAM);

    // 获取接口
    ftx_t key = {.if_name = if_name};
    ftx_t *ftxor = ftx_hash_find(&g_ftx_hash, &key);
    pfm_ensure_ret(ftxor, ERR_NT_IF_ERROR);

    memcpy(mac_addr, ftxor->if_mac, MAC_ADDR_SIZE);

    return ERR_NO_ERROR;
}

error_code_e ftx_ip_get(const char *if_name, uint32_t *ip_addr)
{
    pfm_ensure_ret(ip_addr, ERR_BAD_PARAM);

    // 获取接口
    ftx_t key = {.if_name = if_name};
    ftx_t *ftxor = ftx_hash_find(&g_ftx_hash, &key);
    pfm_ensure_ret(ftxor, ERR_NT_IF_ERROR);

    *ip_addr = ftxor->ipv4_addr.s_addr;

    return ERR_NO_ERROR;
}

static void ftx_early_init(void)
{
    // 初始化哈希表
    ftx_hash_init(&g_ftx_hash);

    // 注册cli查看ftxor
    cli_register("show ftxor", "dump all ftxor", NULL, _ftxor_dump_cli_hook);
}