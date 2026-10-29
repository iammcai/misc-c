/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    zcap.c
 * @brief   抓包实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-25
 * @version 1.1
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-11 | cai | Initial creation.
 *   1.1 | 2026-06-25 | cai | Use event_loop and hash handle for analyzing pkt.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <sys/socket.h>         // for socket
#include <linux/if_packet.h>    // for sockaddr_ll, PACKETV3
#include <linux/if_ether.h>     // for ETH_P_ALL
#include <netinet/in.h>         // for htons
#include <netinet/ip.h>         // for struct iphdr
#include <net/if.h>             // for ifreq
#include <arpa/inet.h>
#include <sys/ioctl.h>          // for ioctl
#include <errno.h>              // for errno
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>           // for mmap
#include <sys/epoll.h>          // for epoll
#include <sys/prctl.h>

#include "zcap/zcap.h"
#include "plat/debug.h"
#include "event/ev_thread.h"
#include "mp/mp_nonfixed.h"
#include "mp/mp_fixed.h"
#include "syslog/syslog.h"
#include "swdg/swdg.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 是否打印报文具体内容
#define DUMP_PKT_DETAIL (0)

// 一个块的大小，mmap要求页对齐，此处设置页*1024
#define BLOCK_SIZE      (1U<<(12+10))       // 4MB
// 块的数量，一共64*4MB=256MB映射
#define BLOCK_NR        (64)
// 帧数量
#define FRAME_NR        (BLOCK_SIZE*BLOCK_NR/FRAME_SIZE)

// 内存池节点数量，通过show mp调整
#define ZCAP_ST_MP_NODE_COUNT   (64)

// 支持抓取的最大长度，标准以太网1500, + MAC 14, + FCS 4
#define ZCAP_MTU_SIZE   (1518)

// 抓包epoll超时 200ms
#define EPOLL_TIMEOUT   (200)

#define err_str         strerror(errno)

#define ZCAP_CAL_RATE_INTERVAL  (1000)      // 计算速率间隔，1s一次

#define ZCAP_ANALYZE_PKTS_ONCE  (1024)      // 每次最多处理1024个报文，避免reactor队头阻塞

#define ZCAP_HASH_Q_MASK        (ZCAP_HASH_Q_SIZE - 1)  // hash分片处理报文的队列掩码

// 打印帧长分布的格式 range count rate
#define ZCAP_DUMP_SIZE_DIST_FMT_HEAD        "%-20s%-16s%-8s\n"
// 打印帧长分布的数据格式
#define ZCAP_DUMP_SIZE_DIST_FMT_DATA        "%-20s%-16llu%-8.3f%\n"

#define ZCAP_DUMP_PKT_TYPE_NUM_HEAD_FMT     "%-16s%-8s\n"
#define ZCAP_DUMP_PKT_TYPE_NUM_FMT          "%-16s%-8u\n"  // 打印各类型报文数量的格式，type num
#define ZCAP_DUMM_PKT_INFO_FMT              "if-%s,len-%-4d: %s\n"      // 打印报文信息格式

// alz线程等待时间
#define ZCAP_ALZ_WAIT_TIME      (500)

// K=1000
#define ZCAP_STATS_K            (1000)
// M=1000000
#define ZCAP_STATS_M            (1000000)
// G=1000000000
#define ZCAP_STATS_G            (1000000000)

// 计算top用的消息队列容量
#define ZCAP_TOP_MSGQ_CAP       (1024)
// 消息队列阻塞时间
#define ZCAP_TOP_MSGQ_WAIT      (100)
// 打印top的个数
#define ZCAP_TOP_CNT            (3)

// show zcaptor 临时缓冲区
#define ZCAP_CLI_BUF_SIZE       (4096)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 内存池，用于rx_t和alz_t之间交互报文，大小4MB
declare_mp_nonfixed(zcap, 4 << 20)
// 用于tcp解析的内存池，在tcp.c中使用
declare_mp_fixed(dissect_tcp, TCP_FLOW_MAX_COUNT, sizeof(tcp_ack_flow_entry_t))

// 封装一下，传递给alz线程入口
typedef struct{
    zcap_t *cap;
    unsigned char index;
}alz_arg_packed;

/* ========================================================================== */
/*                        Static Function Prototypes                          */
/* ========================================================================== */

/**
 * @brief       cmp two zcap_top_src_entry
 */
static attr_pure_inline int _zcap_top_src_entry_cmp(zcap_top_src_entry_t *e1, zcap_top_src_entry_t *e2)
{
    return strncmp((const char*)e1->smac, (const char*)e2->smac, MAC_ADDR_SIZE);
}

/**
 * @brief       cmp for heap
 * 
 * @note        大顶堆，逆序
 */
static attr_pure_inline int _zcap_top_src_entry_cmp_for_heap(zcap_top_src_entry_t *e1, zcap_top_src_entry_t *e2)
{
    if(e1->cnt < e2->cnt)
        return 1;
    else if(e1->cnt > e2->cnt)
        return -1;
    return 0;
}

/**
 * @brief       cmp two zcap_top_src_entry
 */
static attr_pure_inline unsigned int _zcap_top_src_entry_hash(zcap_top_src_entry_t *e)
{
    int64_t mac = *(int64_t*)e->smac;
    return type_hash_jhash(&mac, sizeof(int64_t), 0);
}

// 定义zcap_top_src_entry_t hash操作
declare_hash(zcap_src_top, zcap_src_top, zcap_top_src_entry_t, hash_item, 1, 31, _zcap_top_src_entry_cmp, _zcap_top_src_entry_hash)

// heap操作
declare_heap(zcap_src_top, zcap_src_top, zcap_top_src_entry_t, heap_item, 1, 1024, _zcap_top_src_entry_cmp_for_heap)

/**
 * @brief       cal src top thread entry
 * 
 * @param[in]   zcaptor
 * 
 * @note        负责进行流量来源top计算的线程
 */
static void* _zcap_src_top_cal_routine(void *args);

/**
 * @brief       添加mac记录到src top记录中
 * 
 * @param[in]   top
 * @param[in]   mac     - src mac
 */
static void _zcap_src_top_add(zcap_top_cal_thd_t *top, uint8_t *mac);

/**
 * @brief       统计报文长度
 * 
 * @param[in]   stats       - stats, alz线程独立
 * @param[in]   packet      - packet
 */
static attr_force_inline void _zcap_stats_size_handle(zcap_alz_stats_t *stats, zcap_packet_t *packet);

/**
 * @brief       统计报文uc/mc/bc信息
 * 
 * @param[in]   stats       - stats, alz线程独立
 * @param[in]   packet      - packet
 */
static attr_force_inline void _zcap_stats_cast_handle(zcap_alz_stats_t *stats, zcap_packet_t *packet);

/**
 * @brief       handle protocol and add count
 * 
 * @param[in]   stats       - stats
 * @param[in]   packet      - packet info
 * 
 * @note        解析报文协议，添加协议rx个数，线程独立
 */
static void _zcap_stats_protocol_handle(zcap_alz_stats_t *stats, zcap_packet_t *packet);

/**
 * @brief       cmp two pkt filter
 * 
 * @param[in]   f1  - filter 1
 * @param[in]   f2  - filter 2
 * 
 * @retval      cmp val
 * 
 * @note        cmp by name
 */
static attr_pure_inline int _zcap_pkt_filter_cmp(zcap_pkt_filter_t *f1, zcap_pkt_filter_t *f2)
{
    assert(f1 && f1->name && f2 && f2->name);
    return strcmp(f1->name, f2->name);
}

/**
 * @brief       hash pkt filter
 * 
 * @param[in]   filter
 * 
 * @retval      hash val
 * 
 * @note        hash by name
 */
static attr_pure_inline unsigned int _zcap_pkt_filter_hash(zcap_pkt_filter_t *filter)
{
    assert(filter && filter->name);
    return type_hash_jhash(filter->name, strlen(filter->name), 0);
}

// 定义field链表操作
declare_list(zcap_pkt_field, zcap_pkt_field, zcap_pkt_field_t, item)
// 定义filter哈希表操作
declare_hash(zcap_pkt_filter, zcap_pkt_filter, zcap_pkt_filter_t, item, 1, 31, _zcap_pkt_filter_cmp, _zcap_pkt_filter_hash)

/**
 * @brief       extract packet flow key
 * 
 * @param[in]   packet  - ptr to packet info
 * 
 * @note        提取五元组信息，用于后续哈希分流处理
 */
static attr_force_inline void _zcap_packet_flow_key_extract(zcap_packet_t *packet)
{
    // 检查报文大小是否大于l2长度
    uint32_t len = packet->len;
    if(len < sizeof(struct ethhdr))
    {
        packet->flow_key.err_pkt = 1;
        return;
    }

    const struct ethhdr *eth = (const struct ethhdr*)packet->packet;    // l2头部
    uint16_t ether_type = ntohs(eth->h_proto);
    uint32_t offset = sizeof(struct ethhdr);    // 偏移，用来计算下标是否越界
    zcap_pakcet_flow_key_t *flow_key = &packet->flow_key;

    // 暂时不考虑vlan
    if(ETH_P_IP == ether_type)
    {
        if(offset + sizeof(struct iphdr) > len)     // 检查l3头部是否完整
        {
            packet->flow_key.err_pkt = 1;
            return;
        }
        const struct iphdr *ip = (const struct iphdr*)((uint8_t*)packet->packet + offset);  // l3头部
        uint32_t ip_hdr_len = ip->ihl * 4;      // l3的长度，以4B为单位，包含头部
        if(offset + ip_hdr_len > len)       // 检查l3是否完整
        {
            packet->flow_key.err_pkt = 1;
            return;
        }

        flow_key->src_ip = ip->saddr;
        flow_key->dst_ip = ip->daddr;
        flow_key->protocol = ip->protocol;

        if(IPPROTO_TCP == ip->protocol || IPPROTO_UDP == ip->protocol)  // 处理tcp/udp
        {
            const uint16_t *l4port = (const uint16_t*)((uint8_t*)ip + ip_hdr_len);
            offset += ip_hdr_len;
            if(offset + 4 > len)    // 检查srcport和dstport是否存在
            {
                packet->flow_key.err_pkt = 1;
                return;
            }

            flow_key->src_port = ntohs(l4port[0]);
            flow_key->dst_port = ntohs(l4port[1]);
        }
    }
    else    // 非IPv4报文使用srcMac和etherType作为五元组
    {
        flow_key->protocol = (uint8_t)(ether_type & 0xff);
        memcpy(&flow_key->src_ip, eth->h_source, 4);
    }
}

/**
 * @brief       cal zaptor rx rate thread entry
 */
static void* _zcap_rate_cal_wf(void *args);

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

// 全局哈希表，存放所有zcaptor
static zcaptor_hash_head_t g_zcaptor_hash_head = {};
// lock
static ev_rwlock_t g_zcaptor_hash_rwlock = EV_RWLOCK_INITIALIZER;

// 控制是否打印报文简要信息
static ATOMIC_UINT8_T g_zcap_dump_pkt_info = 0;

// 后台计算rx速率线程tid
static pthread_t g_zcap_rx_rate_cal_tid;
// run
static ATOMIC_UINT8_T g_zcap_rx_rate_cal_tid_run = 0;

// 用于zcap dump的参数
cli_param_t zcap_dump_pkt_param[] = {
    {.required = 1, .short_name = 't', .type = PARAM_VALUE, .help = "means dump period(s)"},
    {},
};

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

// 定义spsc操作
declare_spsc_atom_queue(zcap_packet, zcap_packet, zcap_packet_t, item)

/**
 * @brief       cmp between zcaptors
 * 
 * @param[in]   z1  - captor 1
 * @param[in]   z2  - captor 2
 * 
 * @retval      cmp val
 * 
 * @note        cmp by if_name
 */
static attr_pure_inline int _zcaptor_cmp(zcap_t *z1, zcap_t *z2)
{
    assert(z1 && z1->if_name && z2 && z2->if_name);
    return strcmp(z1->if_name, z2->if_name);
}

/**
 * @brief       hash zcaptor
 * 
 * @param[in]   z   - captor
 * 
 * @retval      hash val
 * 
 * @note        hash by if_name
 */
static attr_pure_inline int _zcaptor_hash(zcap_t *z)
{
    assert(z && z->if_name);
    return type_hash_jhash(z->if_name, strlen(z->if_name), 0);
}

// 定义哈希表操作
declare_hash(zcaptor, zcaptor, zcap_t, item, 1, 4, _zcaptor_cmp, _zcaptor_hash);

/**
 * @brief       trans ether type value to string
 * 
 * @param[in]   ether_type  - et, host order
 * 
 * @retval      string
 */
static attr_pure_inline const char* _zcap_get_ethertype_name(uint16_t ether_type)
{
    switch(ether_type)
    {
        case 0x0800:
            return "ipv4";
        case 0x0806:
            return "arp";
        case 0x86dd:
            return "ipv6";
        default:
            return "unknown";
    }
    return NULL;
}

/**
 * @brief       clean up zcap resource
 * 
 * @param[in]   captor  - captor
 */
static attr_force_inline void _zcap_clean_up(zcap_t *captor)
{
    // 移除哈希表
    ev_with_wrlock(&g_zcaptor_hash_rwlock)
        zcaptor_hash_del(&g_zcaptor_hash_head, captor);

    // 如果抓包正在运行，那么结束它
    unsigned char expected = 1;
    if(ATOM_CMP_XCHG_WEAK(&captor->running, &expected, 0, MORDER_SEQ_SCT, MORDER_RELAXED))
        pthread_join(captor->rx_t, NULL);   // 等待结束

    // 取消映射
    if(MAP_FAILED != captor->ring_buffer)
        munmap(captor->ring_buffer, BLOCK_SIZE*BLOCK_NR);
    captor->ring_len = 0;

    memset(captor->if_mac, 0, MAC_ADDR_SIZE);
    captor->if_index = -1;
    // 关闭socket
    if(captor->sock_fd != -1)
        close(captor->sock_fd);

    ATOM_STORE(&captor->ready, 0, MORDER_RELEASE);
}

/**
 * @brief       setup socket for zcap
 * 
 * @param[in]   captor  - captor
 * 
 * @retval      0 - ok, -1 - not ok
 */
static int _zcap_socket_init(zcap_t *captor)
{
    struct sockaddr_ll addr = {};
    struct ifreq ifr = {};
    struct packet_mreq mr = {};

    // 创建原始套接字，用于捕获所有网络报文
    captor->sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if(-1 == captor->sock_fd)
    {
        dbg_error("socket fail");
        goto clean_up;
    }

    // 获取接口对应的索引还有mac
    strncpy(ifr.ifr_name, captor->if_name, IFNAMSIZ-1);
    if(-1 == ioctl(captor->sock_fd, SIOCGIFINDEX, &ifr))
    {
        dbg_error("ioctl get ifindex fail");
        goto clean_up;
    }
    captor->if_index = ifr.ifr_ifindex;
    if(-1 == ioctl(captor->sock_fd, SIOCGIFHWADDR, &ifr))
    {
        dbg_error("ioctl get hwaddr fail");
        goto clean_up;
    }
    memcpy(captor->if_mac, ifr.ifr_hwaddr.sa_data, MAC_ADDR_SIZE);

    // socket绑定到接口
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ALL);
    addr.sll_ifindex = captor->if_index;
    if(-1 == bind(captor->sock_fd, (struct sockaddr*)&addr, sizeof(addr)))
    {
        dbg_error("bind socket to if fail");
        goto clean_up;
    }

    // 接口开启混杂模式
    mr.mr_ifindex = captor->if_index;
    mr.mr_type = PACKET_MR_PROMISC;
    if(-1 == setsockopt(captor->sock_fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)))
        dbg_error("set promisc module fail, continue");

    dbg("socket init ok, if_name %s, sock_fd %d, if_index %d",
        captor->if_name,
        captor->sock_fd,
        captor->if_index
    );

    return 0;

clean_up:
    _zcap_clean_up(captor);
    return -1;
}

/**
 * @brief       init ring buffer for captor
 * 
 * @param[in]   captor  - captor
 */
static int _zcap_ring_init(zcap_t *captor)
{
    struct tpacket_req3 req = {
        .tp_block_size = BLOCK_SIZE,
        .tp_block_nr = BLOCK_NR,
        .tp_frame_size = FRAME_SIZE,
        .tp_frame_nr = FRAME_NR,
        .tp_retire_blk_tov = 100,   // 超时100ms，避免延迟过大
    };
    unsigned int prot = PROT_READ | PROT_WRITE;
    unsigned int map_flag = MAP_SHARED | MAP_LOCKED;

    // 设置环形缓冲区参数
    int version = TPACKET_V3;
    setsockopt(captor->sock_fd, SOL_PACKET, PACKET_VERSION, &version, sizeof(version));
    if(-1 == setsockopt(captor->sock_fd, SOL_PACKET, PACKET_RX_RING, &req, sizeof(req)))
    {
        perror("fail");
        dbg_error("set rx ring fail, sock_fd %d", captor->sock_fd);
        goto clean_up;
    }

    // 从内核映射
    captor->ring_len = BLOCK_SIZE*BLOCK_NR;
    captor->ring_buffer = mmap(NULL, captor->ring_len, prot, map_flag, captor->sock_fd, 0);
    if(MAP_FAILED == captor->ring_buffer)
    {
        dbg_error("mmap ring buffer fail");
        goto clean_up;
    }

    dbg("rx ring buffer init ok, page_size %u KB, blocks=%u, block_size=%u MB, total=%u MB",
        sysconf(_SC_PAGESIZE)/1024, BLOCK_NR, BLOCK_SIZE / 1024 /1024, captor->ring_len / 1024 / 1024
    );

    return 0;

clean_up:
    _zcap_clean_up(captor);
    return -1;
}

/**
 * @brief       zcap thread work func
 * 
 * @param[in]   args    - captor ptr
 * 
 * @note        循环抓包线程
 */
static void* _zcap_routine(void *args)
{
    zcap_t *captor = (zcap_t*)args;

    char buf[16] = {};
    snprintf(buf, sizeof(buf), "zcap_%s", captor->if_name);
    prctl(PR_SET_NAME, buf);

    // 创建epoll et，监听抓包socket
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if(-1 == epoll_fd)
    {
        dbg_error("create epoll fail: %s", err_str);
        goto clean_up;
    }

    // epoll et监听socket fd
    struct epoll_event ev = {
        .events = EPOLLIN | EPOLLET,
        .data.fd = captor->sock_fd,
    };
    if(-1 == epoll_ctl(epoll_fd, EPOLL_CTL_ADD, captor->sock_fd, &ev))
    {
        dbg_error("epoll listen fd %d fail: %s", captor->sock_fd, err_str);
        goto clean_up;
    }

    // 注册看门狗
    swdg_register(buf, EPOLL_TIMEOUT * 3);

    dbg_major("start capture rx thread");

    unsigned int block_idx = 0;     // 环形缓冲区下标
    while(ATOM_LOAD(&captor->running, MORDER_ACQUIRE))  // 主循环，检查running标志
    {
        // 喂狗
        swdg_feed();

        // 等待内核通知block就绪
        struct epoll_event evs[1];
        int n = epoll_wait(epoll_fd, evs, 1, EPOLL_TIMEOUT);    // 200ms用于检查running
        if(n == 0)      // 超时
            continue;
        else if(n < 0)
        {
            if(errno == EINTR)  // 信号干扰
                continue;
            dbg_error("epoll wait fail: %s", err_str);
            break;
        }

        // 遍历就绪block，ET必须一次性全部处理完
        while(1)
        {
            struct tpacket_block_desc *pbd = (struct tpacket_block_desc*)((char*)captor->ring_buffer + block_idx * BLOCK_SIZE);

            // 检查就绪标志
            if(!(pbd->hdr.bh1.block_status & TP_STATUS_USER))
                break;

            // 遍历block内的所有帧
            unsigned int num_pkts = pbd->hdr.bh1.num_pkts;  // 报文数量
            struct tpacket3_hdr *ppd = (struct tpacket3_hdr*)((char*)pbd + pbd->hdr.bh1.offset_to_first_pkt);   // 偏移到第一个报文
            unsigned int i = 0;
            for(i = 0; i < num_pkts; ++ i)
            {
                const unsigned char *pkt_data = (const unsigned char*)ppd + ppd->tp_mac;    // mac
                unsigned int pkt_len = ppd->tp_snaplen;     // 报文长度
                // 移动到下一个报文
                ppd = (struct tpacket3_hdr*)((char*)ppd + ppd->tp_next_offset);

                // 过滤掉本机自环报文，src mac为本机的写入到dma的，不是真正的rx
                if(!memcmp(pkt_data + MAC_ADDR_SIZE, captor->if_mac, MAC_ADDR_SIZE))
                    continue;

                // 构建消息，将报文发给解析线程
                zcap_packet_t *packet = mp_nonfixed_node_get(zcap, sizeof(zcap_packet_t)+pkt_len);
                if(!packet)
                {
                    char syslog_ctx[SYSLOG_ENTRY_CTX_SIZE] = {};
                    snprintf(syslog_ctx, sizeof(syslog_ctx), "get mem node for size %zu fail, packet loss", 
                        sizeof(zcap_packet_t) + pkt_len);
                    syslog_warn(SYSLOG_MODULE_ZCAP, syslog_ctx);
                    continue;
                }
                memset(packet, 0, sizeof(zcap_packet_t)+pkt_len);
                memcpy(packet->packet, pkt_data, pkt_len);  // 复制内容
                packet->len = pkt_len;
                // 提取五元组信息
                _zcap_packet_flow_key_extract(packet);
                // 根据五元组信息计算Hash发送给对应解析线程
                unsigned long hashval = type_hash_jhash(&packet->flow_key, sizeof(zcap_pakcet_flow_key_t), 0);
                hashval &= ZCAP_HASH_Q_MASK;
                zcap_packet_spsc_atom_queue_push(&captor->alz[hashval].alz_aq_head, packet);
            }
            // block一批结束后统一唤醒
            for(int q = 0; q < ZCAP_HASH_Q_SIZE; ++ q)
                ev_sem_post(&captor->alz[q].alz_sem);

            // 归还Block
            pbd->hdr.bh1.block_status = TP_STATUS_KERNEL;

            // 继续处理block
            block_idx = (block_idx+1)%BLOCK_NR;
        }
    }

    dbg_major("capture rx thread end");

clean_up:
    // 关闭epoll
    if(-1 != epoll_fd)
        close(epoll_fd);        // 包含EPOLL_CTL_DEL
    return NULL;
}

/**
 * @brief       提取报文信息，用来匹配过滤器
 * 
 * @param[in]   packet
 * 
 * @param[out]  info    - packet info
 */
static void _zcap_pkt_info_extract(zcap_packet_t *packet, zcap_pkt_info_t *info)
{
    // 错包不做解析
    if(packet->flow_key.err_pkt)
        return;

    memset(info, 0, sizeof(zcap_pkt_info_t));

    // 提取l2信息
    const struct ethhdr *eth = (const struct ethhdr*)packet->packet;    // l2头部
    memcpy(info->mac_da, eth->h_dest, MAC_ADDR_SIZE);
    memcpy(info->mac_sa, eth->h_source, MAC_ADDR_SIZE);
    info->ether_type = ntohs(eth->h_proto);

    uint32_t offset = sizeof(struct ethhdr);    // 偏移，用来计算下标是否越界

    // 暂时不考虑vlan
    if(ETH_P_IP == info->ether_type)
    {
        const struct iphdr *ip = (const struct iphdr*)((uint8_t*)packet->packet + offset);  // l3头部

        info->ip_pro = ip->protocol;

        if(IPPROTO_TCP == ip->protocol || IPPROTO_UDP == ip->protocol)  // 处理tcp/udp
        {
            info->l4_sport = packet->flow_key.src_port;
            info->l4_dport = packet->flow_key.dst_port;
        }
    }
}

/**
 * @brief       check if packet field match info
 * 
 * @param[in]   field   - packet field
 * @param[in]   info    - packet info, extract by _zcap_pkt_info_extract
 * 
 * @retval      1 - match, 0 - not match
 */
static int _zcap_pkt_filed_match(zcap_pkt_field_t *field, const zcap_pkt_info_t *info)
{
    switch(field->field)
    {
        case MAC_DA:
        {
            uint8_t key[FIELD_DATA_SIZE] = {};
            int i = 0;
            for(; i < FIELD_DATA_SIZE; ++ i)
                key[i] = field->data[i] & field->mask[i];
            if(!memcmp(key, info->mac_da, MAC_ADDR_SIZE))
                return 1;
            break;
        }
        case MAC_SA:
        {
            uint8_t key[FIELD_DATA_SIZE] = {};
            int i = 0;
            for(; i < FIELD_DATA_SIZE; ++ i)
                key[i] = field->data[i] & field->mask[i];
            if(!memcmp(key, info->mac_sa, MAC_ADDR_SIZE))
                return 1;
            break;
        }
        case LEN_TYPE:
        {
            uint16_t key = (uint16_t)((field->data[0] & field->mask[0]) << 8) | (field->data[1] & field->mask[1]);
            if(key == info->ether_type)
                return 1;
            break;
        }
        case IP_DA:
        {
            uint8_t key[IPV4_ADDR_SIZE] = {};
            int i = 0;
            for(; i < IPV4_ADDR_SIZE; ++ i)
                key[i] = field->data[i] & field->mask[i];
            uint32_t ip_da_h = (key[0] << 24) | (key[1] << 16) | (key[2] << 8) | key[3];
            if(ip_da_h == info->ip_da)
                return 1;
            break;
        }
        case IP_SA:
        {
            uint8_t key[IPV4_ADDR_SIZE] = {};
            int i = 0;
            for(; i < IPV4_ADDR_SIZE; ++ i)
                key[i] = field->data[i] & field->mask[i];
            uint32_t ip_sa_h = (key[0] << 24) | (key[1] << 16) | (key[2] << 8) | key[3];
            if(ip_sa_h == info->ip_sa)
                return 1;
            break;
        }
        case PRO_TYPE:
        {
            uint8_t key = field->data[0] & field->mask[0];
            if(key == info->ip_pro)
                return 1;
            break;
        }
        case DPORT:
        {
            uint16_t key = ((field->data[0] & field->mask[0]) << 8) | (field->data[1] & field->mask[1]);
            if(key == info->l4_dport)
                return 1;
            break;
        }
        case SPORT:
        {
            uint16_t key = ((field->data[0] & field->mask[0]) << 8) | (field->data[1] & field->mask[1]);
            if(key == info->l4_sport)
                return 1;
            break;
        }
        default:
            return 0;
    }

    return 0;
}

/**
 * @brief       analyze a packet
 * 
 * @param[in]   captor  - zcaptor
 * @param[in]   packet  - packet info
 * 
 * @note        解析单个报文
 */
static void _zcap_analyze_a_packet(zcap_t *captor, zcap_packet_t *packet)
{
    const struct ethhdr *eth = (struct ethhdr*)packet->packet;

    // 如果打开了信息调试，那么输出报文信息到CLI
    if(ATOM_LOAD(&g_zcap_dump_pkt_info, MORDER_ACQUIRE))
    {
        char info[256] = {};    // 临时空间，用一下魔数
        snprintf(info, 256, "from %02x-%02x-%02x-%02x-%02x-%02x, type %04x",
            eth->h_source[0], eth->h_source[1], eth->h_source[2], eth->h_source[3], eth->h_source[4], eth->h_source[5],
            ntohs(eth->h_proto)
        );
        safe_printf(ZCAP_DUMM_PKT_INFO_FMT, captor->if_name, packet->len, info);
    }

    // src mac信息发给top进程记录
    _msg_q_push(captor->top.msgq, (void*)eth->h_source, MAC_ADDR_SIZE);

    // 提取过滤器所需要的信息
    zcap_pkt_info_t packet_info = {};
    _zcap_pkt_info_extract(packet, &packet_info);
    // 遍历注册的过滤条件，执行注册回调
    zcap_pkt_filter_t *filter = zcap_pkt_filter_hash_first(&captor->filters);
    while(filter)
    {
        // 遍历field链表，逐个检查字段是否满足
        int match = 1;
        zcap_pkt_field_t *field = zcap_pkt_field_list_first(&filter->fields);
        while(field)
        {
            match = _zcap_pkt_filed_match(field, &packet_info);
            if(!match)
                break;
            // 检查下一个field
            field = zcap_pkt_field_list_next(field);
        }
        if(match)
        {
            filter->cb(packet->packet, packet->len, filter->args);
            ATOM_FETCH_ADD(&filter->match_cnt, 1, MORDER_RELAXED);      // 命中计数++
        }

        // 继续检查下一个过滤器
        filter = zcap_pkt_filter_hash_next(&captor->filters, filter);
    }
}

/**
 * @brief       analyze thread routine
 * 
 * @param[in]   zcaptor
 * 
 * @note        由evloop_cb根据hash分发过来的线程进行处理
 */
static void* _zcap_alz_routine(void *args)
{
    alz_arg_packed* arg = (alz_arg_packed*)args;
    unsigned char i = arg->index;
    zcap_t *captor = arg->cap;
    zcap_packet_t *packet = NULL;

    char buf[16] = {};
    snprintf(buf, sizeof(buf), "zc_alz_%s_%d", captor->if_name, i);
    prctl(PR_SET_NAME, buf);
    swdg_register(buf, ZCAP_ALZ_WAIT_TIME * 3);

    mp_free(args, sizeof(alz_arg_packed));   // 释放入参

    tcp_dissect_init();     // 初始化tcp流表

    while(ATOM_LOAD(&captor->running, MORDER_ACQUIRE))
    {
        swdg_feed();
        // 先尝试批量 drain 队列
        int processed = 0;
        while((packet = zcap_packet_spsc_atom_queue_pop(&captor->alz[i].alz_aq_head)))
        {
            // 更新本线程stat
            captor->alz[i].stats.counts ++;
            captor->alz[i].stats.bytes += packet->len;

            // 报文长度处理
            _zcap_stats_size_handle(&captor->alz[i].stats, packet);
            // cast类型
            _zcap_stats_cast_handle(&captor->alz[i].stats, packet);
            // 预置协议计数处理
            _zcap_stats_protocol_handle(&captor->alz[i].stats, packet);

            // 模块分发
            _zcap_analyze_a_packet(captor, packet);
            mp_nonfixed_node_put(packet);
            processed++;
        }

        if(processed == 0)
            ev_sem_wait_ms(&captor->alz[i].alz_sem, ZCAP_ALZ_WAIT_TIME);
    }

    dbg("zcaptor %s analyze-%d thread exit", captor->if_name, i);

    tcp_dissect_exit();         // 销毁tcp流表

    return NULL;
}

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

static void _zcap_src_top_add(zcap_top_cal_thd_t *top, uint8_t *mac)
{
    pfm_ensure_ret_void(top && mac);

    zcap_top_src_entry_t key = {};
    memcpy(key.smac, mac, MAC_ADDR_SIZE);

    ev_with_spinlock(&top->lock)
    {
        zcap_top_src_entry_t *entry = zcap_src_top_hash_find(&top->hashtable, &key);
        if(unlikely(!entry))
        {
            entry = mp_calloc(1, sizeof(zcap_top_src_entry_t));
            memcpy(entry->smac, mac, MAC_ADDR_SIZE);
            zcap_src_top_hash_add(&top->hashtable, entry);
        }
        ++ entry->cnt;      // 计数++
        // dbg_always("mac %02x:%02x:%02x:%02x:%02x:%02x, cnt %d", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], entry->cnt);
    }
}

static void* _zcap_src_top_cal_routine(void *args)
{
    zcap_t *zcaptor = (zcap_t*)args;
    zcap_top_cal_thd_t *top = &zcaptor->top;

    // 资源初始化
    ev_spinlock_init(&top->lock);
    zcap_src_top_hash_init(&top->hashtable);
    top->msgq = msg_q_create("zcap_top_src_msgq", ZCAP_TOP_MSGQ_CAP, sizeof(uint8_t)*MAC_ADDR_SIZE);

    char name[16];
    snprintf(name, 15, "zcap_%s_top", zcaptor->if_name);
    prctl(PR_SET_NAME, name);

    swdg_register(name, ZCAP_TOP_MSGQ_WAIT * 3);       // reg swdg

    ATOM_STORE(&top->run, 1, MORDER_RELAXED);
    while(ATOM_LOAD(&top->run, MORDER_RELAXED))
    {
        swdg_feed();

        uint8_t mac[MAC_ADDR_SIZE];
        msg_q_ret_type_e ret = _msg_q_pop(top->msgq, sizeof(uint8_t)*MAC_ADDR_SIZE, msg_q_wait_timeout, ZCAP_TOP_MSGQ_WAIT, mac);
        // 处理消息
        if(ret == msg_q_ret_ok)
            _zcap_src_top_add(top, mac);
    }

    return NULL;
}

static void _zcap_stats_size_handle(zcap_alz_stats_t *stats, zcap_packet_t *packet)
{
    int len = packet->len;
    if(len >= 1518)         ++ stats->size_gt_1518;
    else if(len >= 1024)    ++ stats->size_1024_1518;
    else if(len >= 512)     ++ stats->size_512_1024;
    else if(len >= 256)     ++ stats->size_256_512;
    else if(len >= 128)     ++ stats->size_128_256;
    else if(len >= 64)      ++ stats->size_64_128;
    else                    ++ stats->size_lt_64;
}

static void _zcap_stats_cast_handle(zcap_alz_stats_t *stats, zcap_packet_t *packet)
{
    pkthdr_l2_t *l2 = (pkthdr_l2_t*)(packet->packet);
    uint8_t bc_mac_addr[MAC_ADDR_SIZE] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    if(0 == memcmp(l2->mac_da, bc_mac_addr, MAC_ADDR_SIZE))
        stats->bc_count ++;
    else if(l2->mac_da[0] & 0x01)
        stats->mc_count ++;
    else
        stats->uc_count ++;
}

static void _zcap_stats_protocol_handle(zcap_alz_stats_t *stats, zcap_packet_t *packet)
{
    pfm_ensure_ret_void(stats && packet);

    size_t offset = 0;
    size_t total_size = packet->len;
    char *ctx = packet->packet;

    // l2
    if((offset + PKT_HDR_L2_SIZE) > total_size)
        goto others;
    pkthdr_l2_t *l2_hdr = (pkthdr_l2_t*)ctx;
    uint16_t ethertype = ntohs(l2_hdr->ether_type);
    offset += PKT_HDR_L2_SIZE;
    ctx += PKT_HDR_L2_SIZE;

    if(ethertype == ARP)
    {
        stats->arp ++;
        return;
    }
    else if(ethertype == IPV4)
    {
        // l3
        if(offset + sizeof(pkthdr_ipv4_t) > total_size)
            goto others;
        pkthdr_ipv4_t *ipv4_hdr = (pkthdr_ipv4_t*)ctx;
        uint8_t ihl = IPV4_HEADER_SIZE(ipv4_hdr);       // 计算实际的ipv4 header长度
        offset += ihl;
        ctx += ihl;

        uint8_t protocol = ipv4_hdr->protocol;
        if(protocol == ICMP)
        {
            stats->icmp ++;
            return;
        }
        else if(protocol == TCP)
        {
            if(offset + sizeof(pkthdr_tcp_t) > total_size)
                goto others;
            pkthdr_tcp_t *tcp_hdr = (pkthdr_tcp_t*)ctx;
            uint8_t tcp_hdr_len = TCP_HEADER_SIZE(tcp_hdr);     // 实际tcp header长度
            offset += tcp_hdr_len;
            ctx += tcp_hdr_len;

            tcp_dissect(packet->packet, packet->len, &stats->tcp);

            uint16_t dport = ntohs(tcp_hdr->dst_port);
            if(dport == SSH_TCP_PORT)
            {
                stats->ssh ++;
                return;
            }
            else if(dport == MISC_C_WEB_TCP_PORT)
            {
                stats->miscc_web ++;
                return;
            }
            else
            {
                stats->tcp_other ++;
                return;
            }
        }
        else if(protocol == UDP)
        {
            if(offset + sizeof(pkthdr_udp_t) > total_size)
                goto others;
            pkthdr_udp_t *udp_hdr = (pkthdr_udp_t*)ctx;
            offset += sizeof(pkthdr_udp_t);
            ctx += sizeof(pkthdr_udp_t);

            uint16_t dport = ntohs(udp_hdr->dport);
            if(dport == DNS_UDP_PORT)                                   // dns
            {
                stats->dns ++;
                return;
            }
            else if(dport == DHCP_UDP_CLIENT_PORT || dport == DHCP_UDP_SERVER_PORT)     // dhcp
            {
                stats->dhcp ++;
                return;
            }
            else
            {
                stats->udp_other ++;
                return;
            }
        }
        else
            goto others;
    }

others:
    stats->others ++;
    return;
}

void zcap_module_init()
{
    zcaptor_hash_init(&g_zcaptor_hash_head);

    // 内存池注册
    mp_nonfixed_register(zcap);
    mem_type_attr_fixed_register(dissect_tcp);

    // 启动线程计算zcaptor rx速率
    pthread_create(&g_zcap_rx_rate_cal_tid, NULL, _zcap_rate_cal_wf, NULL);

    dbg_major("zcap module init ok");
}

void zcap_module_exit_th()
{
    // 遍历哈希表，停止running中的抓包
    ev_with_wrlock(&g_zcaptor_hash_rwlock)
    {
        zcap_t *captor = zcaptor_hash_first(&g_zcaptor_hash_head);
        while(captor)
        {
            if(ATOM_LOAD(&captor->running, MORDER_RELAXED))
            {
                ATOM_STORE(&captor->running, 0, MORDER_RELAXED);
                pthread_join(captor->rx_t, NULL);
                dbg("stop captor %s rx_t ok", captor->if_name);
            }
            captor = zcaptor_hash_next(&g_zcaptor_hash_head, captor);
        }
    }

    dbg_major("zcap module exit top half ok");
}

void zcap_module_exit_bh()
{
    ev_with_wrlock(&g_zcaptor_hash_rwlock)
    {
        while(zcaptor_hash_count(&g_zcaptor_hash_head))
        {
            zcap_t *captor = zcaptor_hash_first(&g_zcaptor_hash_head);
            zcaptor_hash_del(&g_zcaptor_hash_head, captor);

            // 结束alz线程，此时已经running设置为0，发送信号量唤醒
            for(int i = 0; i < ZCAP_HASH_Q_SIZE; ++ i)
            {
                if(captor->alz[i].alz_t != 0)
                {
                    ev_sem_post(&captor->alz[i].alz_sem);
                    pthread_join(captor->alz[i].alz_t, NULL);
                }
            }

            // 销毁过滤条件链表以及哈希表
            zcap_pkt_filter_t *filter = zcap_pkt_filter_hash_first(&captor->filters);
            while(filter)
            {
                // 回收链表资源
                zcap_pkt_field_t *field = NULL;
                while(field = zcap_pkt_field_list_pop(&filter->fields))
                    mp_free(field, sizeof(zcap_pkt_field_t));
                zcap_pkt_field_list_fini(&filter->fields);

                // 从Hash中删除
                zcap_pkt_filter_hash_del(&captor->filters, filter);
                mp_free(filter, sizeof(zcap_pkt_filter_t));     // 清除内存
                // 继续拿出Hash head
                filter = zcap_pkt_filter_hash_first(&captor->filters);
            }
            zcap_pkt_filter_hash_fini(&captor->filters);

            // 释放其它zcap资源
            if(MAP_FAILED != captor->ring_buffer)
                munmap(captor->ring_buffer, BLOCK_SIZE * BLOCK_NR);
            if(captor->sock_fd >= 0)
                close(captor->sock_fd);
            for(int i = 0; i < ZCAP_HASH_Q_SIZE; ++ i)
            {
                zcap_packet_t *packet;
                while(packet = zcap_packet_spsc_atom_queue_pop(&captor->alz[i].alz_aq_head))
                    mp_nonfixed_node_put(packet);
                zcap_packet_spsc_atom_queue_fini(&captor->alz[i].alz_aq_head);
            }
        }
    }

    // 结束rate线程
    ATOM_STORE(&g_zcap_rx_rate_cal_tid_run, 0, MORDER_RELAXED);
    pthread_join(g_zcap_rx_rate_cal_tid, NULL);

    dbg_major("zcap module de init bottom half ok");
}

void _zcap_init(zcap_t *captor)
{
    // 获取接口真实状态，linkup且分配ipv4地址的才可初始化抓包
    if_status_t status = {};
    int ret = if_status_get(captor->if_name, &status);
    if(ERR_NO_ERROR != ret || status.has_ipv4 == 0 || status.linkup == 0)
    {
        dbg_error("if %s status check fail, zcaptor init fail", captor->if_name);
        return;
    }

    // 加到哈希表
    ev_with_wrlock(&g_zcaptor_hash_rwlock)
        zcaptor_hash_add(&g_zcaptor_hash_head, captor);

    // 初始化过滤哈希表
    zcap_pkt_filter_hash_init(&captor->filters);

    // rx_t、alz_t spsc队列初始化
    unsigned char i = 0;
    for(; i < ZCAP_HASH_Q_SIZE; ++ i)
    {
        zcap_packet_spsc_atom_queue_init(&(captor->alz[i].alz_aq_head));
        ev_sem_init(&captor->alz[i].alz_sem);
        memset(&captor->alz[i].stats, 0, sizeof(zcap_alz_stats_t));
    }

    // 统计数据初始化
    memset(&captor->stats, 0, sizeof(zcap_stats_t));
    ev_spinlock_init(&captor->stats.lock);

    captor->ring_buffer = MAP_FAILED;

    if(-1 == _zcap_socket_init(captor))     // 设置socket
        goto error;

    if(-1 == _zcap_ring_init(captor))       // 设置环形缓冲区
        goto error;

    ATOM_STORE(&captor->ready, 1, MORDER_RELEASE);      // 设置完成标志

    dbg_major("zcaptor(if_name=%s) init ok", captor->if_name);

    return;

error:
    dbg_error("zcaptor init fail");
}

void _zcap_start(zcap_t *captor)
{
    // 检查是否初始化完毕，以及线程是否在工作
    if(!ATOM_LOAD(&captor->ready, MORDER_ACQUIRE) || ATOM_LOAD(&captor->running, MORDER_ACQUIRE))
        return;

    unsigned char expected = 0;
    if(!ATOM_CMP_XCHG_WEAK(&captor->running, &expected, 1, MORDER_SEQ_SCT, MORDER_RELAXED))
        return;

    // 创建alz线程
    unsigned char i = 0;
    for(; i < ZCAP_HASH_Q_SIZE; ++ i)
    {
        alz_arg_packed *arg = mp_calloc(1, sizeof(alz_arg_packed));
        arg->cap = captor;
        arg->index = i;
        pthread_create(&captor->alz[i].alz_t, NULL, _zcap_alz_routine, arg);
    }

    // 创建src top统计线程
    pthread_create(&captor->top.tid, NULL, _zcap_src_top_cal_routine, captor);
    // 等待线程启动
    while(!ATOM_LOAD(&captor->top.run, MORDER_RELAXED)) usleep(100);

    if(-1 == pthread_create(&captor->rx_t, NULL, _zcap_routine, captor))    // 新建抓包线程
    {
        dbg_error("create rx_t fail");
        ATOM_STORE(&captor->running, 0, MORDER_RELEASE);
    }
}

void _zcap_cancel(zcap_t *captor)
{
    unsigned char expected = 1;

    if(ATOM_CMP_XCHG_WEAK(&captor->running, &expected, 0, MORDER_SEQ_SCT, MORDER_RELAXED))
    {
        pthread_join(captor->rx_t, NULL);   // 等待结束
        dbg_major("capture rx thread end");

        // 结束解析线程
        unsigned char i = 0;
        for(; i < ZCAP_HASH_Q_SIZE; ++ i)
        {
            ev_sem_post(&captor->alz[i].alz_sem);   // 唤醒进行检查
            pthread_join(captor->alz[i].alz_t, NULL);
            dbg_major("alz thread %d end", i);
        }
    }
}

int zcap_status_get(const char *if_name)
{
    pfm_ensure_ret(if_name, 0);

    zcap_t captor_key = {.if_name = if_name};
    zcap_t *zcaptor = NULL;
    ev_with_rdlock(&g_zcaptor_hash_rwlock)
        zcaptor = zcaptor_hash_find(&g_zcaptor_hash_head, &captor_key);
    pfm_ensure_ret(zcaptor, 0);

    return ATOM_LOAD(&zcaptor->running, MORDER_RELAXED);
}

void _zcap_register_pkt_filter(const char *if_name, const char *filter_name, zcap_pkt_filter_match_cb cb, void *args)
{
    assert(if_name && filter_name && cb);

    // 查找zcaptor
    zcap_t captor_key = {.if_name = if_name};
    zcap_t *zcaptor = NULL;
    ev_with_rdlock(&g_zcaptor_hash_rwlock)
        zcaptor = zcaptor_hash_find(&g_zcaptor_hash_head, &captor_key);
    if(!zcaptor)        // 查找失败，返回
        return;

    zcap_pkt_filter_t key = {.name = filter_name};
    zcap_pkt_filter_t *filter = zcap_pkt_filter_hash_find(&zcaptor->filters, &key);
    if(filter)      // 存在的话不做修改
        return;

    filter = (zcap_pkt_filter_t*)mp_calloc(1, sizeof(zcap_pkt_filter_t));
    filter->name = mp_strdup(filter_name);
    filter->enable = 1;     // 默认使能
    filter->cb = cb;
    filter->args = args;
    // 初始化field链表
    zcap_pkt_field_list_init(&filter->fields);
    // filter加入哈希表
    zcap_pkt_filter_hash_add(&zcaptor->filters, filter);
}

void _zcap_register_field(const char *if_name, const char *filter_name, zcap_pkt_field_e field, uint8_t *data, uint8_t *mask)
{
    assert(if_name && filter_name && data && mask);

    // 查找zcaptor
    zcap_t captor_key = {.if_name = if_name};
    zcap_t *zcaptor = NULL;
    ev_with_rdlock(&g_zcaptor_hash_rwlock)
        zcaptor = zcaptor_hash_find(&g_zcaptor_hash_head, &captor_key);
    if(!zcaptor)    // 查找失败则返回
        return;

    // 查找过滤器
    zcap_pkt_filter_t key = {.name = filter_name};
    zcap_pkt_filter_t *filter = zcap_pkt_filter_hash_find(&zcaptor->filters, &key);
    assert(filter);

    // 构造新的field
    zcap_pkt_field_t *new_field = (zcap_pkt_field_t*)mp_calloc(1, sizeof(zcap_pkt_field_t));
    new_field->field = field;
    memcpy(new_field->data, data, FIELD_DATA_SIZE);
    memcpy(new_field->mask, mask, FIELD_MASK_SIZE);
    // 字段添加到链表中
    zcap_pkt_field_list_add_tail(&filter->fields, new_field);
}

void zcap_dump_captor_cli_hook(cli_ctx_t *ctx, unsigned char argc, char *argv[])
{
    char temp_buf[ZCAP_CLI_BUF_SIZE] = {};
    ev_with_rdlock(&g_zcaptor_hash_rwlock)
    {
        zcap_t *captor = zcaptor_hash_first(&g_zcaptor_hash_head);
        while(captor)
        {
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), "\n********************************\n");
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), 
                "if_name: %s\nif_index: %d\nif_mac: %02x-%02x-%02x-%02x-%02x-%02x\n", 
                captor->if_name, captor->if_index,
                captor->if_mac[0],captor->if_mac[1],captor->if_mac[2],
                captor->if_mac[3],captor->if_mac[4],captor->if_mac[5]
            );
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), 
                "runing flag: %d\n\n", ATOM_LOAD(&captor->running, MORDER_ACQUIRE));

            // 打印统计数据，收集per-thread stats
            uint64_t total_counts = 0;
            uint64_t total_bytes = 0;
            uint64_t uc_counts = 0;
            uint64_t mc_counts = 0;
            uint64_t bc_counts = 0;
            uint64_t size_lt_64 = 0;
            uint64_t size_64_128 = 0;
            uint64_t size_128_256 = 0;
            uint64_t size_256_512 = 0;
            uint64_t size_512_1024 = 0;
            uint64_t size_1024_1518 = 0;
            uint64_t size_gt_1518 = 0;
            for(int q = 0; q < ZCAP_HASH_Q_SIZE; ++ q)
            {
                total_counts += captor->alz[q].stats.counts;
                total_bytes += captor->alz[q].stats.bytes;
                uc_counts += captor->alz[q].stats.uc_count;
                mc_counts += captor->alz[q].stats.mc_count;
                bc_counts += captor->alz[q].stats.bc_count;
                size_lt_64 += captor->alz[q].stats.size_lt_64;
                size_64_128 += captor->alz[q].stats.size_64_128;
                size_128_256 += captor->alz[q].stats.size_128_256;
                size_256_512 += captor->alz[q].stats.size_256_512;
                size_512_1024 += captor->alz[q].stats.size_512_1024;
                size_1024_1518 += captor->alz[q].stats.size_1024_1518;
                size_gt_1518 += captor->alz[q].stats.size_gt_1518;
            }
            uint64_t total = size_lt_64 + size_64_128 + size_128_256 + size_256_512 + size_512_1024 + size_1024_1518 + size_gt_1518;
            if(total_counts >= ZCAP_STATS_G)
                snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), 
                    "Total counts: %.2f Gpkts\n", (double)total_counts/ZCAP_STATS_G);
            else if(total_counts >= ZCAP_STATS_M)
                snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), 
                    "Total counts: %.2f Mpkts\n", (double)total_counts/ZCAP_STATS_M);
            else if(total_counts >= ZCAP_STATS_K)
                snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), 
                    "Total counts: %.2f Kpkts\n", (double)total_counts/ZCAP_STATS_K);
            else
                snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), 
                    "Total counts: %llu pkts\n", total_counts);
            if(total_bytes >= ZCAP_STATS_G)
                snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), 
                    "Total Bytes: %.2f GB\n", (double)total_bytes/ZCAP_STATS_G);
            else if(total_bytes >= ZCAP_STATS_M)
                snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), 
                    "Total Bytes: %.2f MBs\n", (double)total_bytes/ZCAP_STATS_M);
            else if(total_bytes >= ZCAP_STATS_K)
                snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), 
                    "Total Bytes: %.2f KB\n", (double)total_bytes/ZCAP_STATS_K);
            else
                snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), 
                    "Total Bytes: %llu B\n", total_bytes);
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), 
                "Average Size: %.2f Bytes\n", (double)total_bytes/total_counts);
            ev_with_spinlock(&captor->stats.lock)
                snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), 
                    "Rate: %.1f pkts/s, %.1f Bytes/s\n", captor->stats.pps, captor->stats.Bps);

            // 帧长分布
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), "\n---------------------------------------------\n");
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), ZCAP_DUMP_SIZE_DIST_FMT_HEAD, "size", "counts", "rate");
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), ZCAP_DUMP_SIZE_DIST_FMT_HEAD, "------", "------", "----");
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), ZCAP_DUMP_SIZE_DIST_FMT_DATA, "< 64", size_lt_64, (double)size_lt_64/total*100.0);
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), ZCAP_DUMP_SIZE_DIST_FMT_DATA, "64 ~ 127", size_64_128, (double)size_64_128/total*100.0);
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), ZCAP_DUMP_SIZE_DIST_FMT_DATA, "128 ~ 255", size_128_256, (double)size_128_256/total*100.0);
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), ZCAP_DUMP_SIZE_DIST_FMT_DATA, "256 ~ 511", size_256_512, (double)size_256_512/total*100.0);
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), ZCAP_DUMP_SIZE_DIST_FMT_DATA, "512 ~ 1023", size_512_1024, (double)size_512_1024/total*100.0);
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), ZCAP_DUMP_SIZE_DIST_FMT_DATA, "1024 ~ 1517", size_1024_1518, (double)size_1024_1518/total*100.0);
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), ZCAP_DUMP_SIZE_DIST_FMT_DATA, "> 1518", size_gt_1518, (double)size_gt_1518/total*100.0);
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), "---------------------------------------------\n");

            // L2信息
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), 
                "\nL2 Info\nUnicast Counts: %llu\nMulticast Counts: %llu\nBroadcast Counts: %llu\n",
                uc_counts, mc_counts, bc_counts);

            // 打印协议统计数据
            uint64_t arp = 0;
            uint64_t icmp = 0;
            uint64_t miscc_web = 0;
            uint64_t ssh = 0;
            uint64_t dns = 0;
            uint64_t dhcp = 0;
            uint64_t tcp_other = 0;
            uint64_t udp_other = 0;
            uint64_t others = 0;
            for(int q = 0; q < ZCAP_HASH_Q_SIZE; ++ q)
            {
                arp += captor->alz[q].stats.arp;
                icmp += captor->alz[q].stats.icmp;
                miscc_web += captor->alz[q].stats.miscc_web;
                ssh += captor->alz[q].stats.ssh;
                dns += captor->alz[q].stats.dns;
                dhcp += captor->alz[q].stats.dhcp;
                tcp_other += captor->alz[q].stats.tcp_other;
                udp_other += captor->alz[q].stats.udp_other;
                others += captor->alz[q].stats.others;
            }
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), 
                "\nProtocol Counts:\n"
                "arp:           %d\n"
                "icmp:          %d\n"
                "miscc_web:     %d\n"
                "ssh:           %d\n"
                "dns:           %d\n"
                "dhcp:          %d\n"
                "tcp_other:     %d\n"
                "udp_other:     %d\n"
                "others:        %d\n",
                arp, icmp, miscc_web, ssh, dns, dhcp, tcp_other, udp_other, others
            );

            // tcp分布
            uint64_t tcp_total = 0;
            uint64_t _tcp_ack_total = 0;
            uint64_t tcp_pure_syn = 0;
            uint64_t tcp_syn_ack = 0;
            uint64_t tcp_rst = 0;
            uint64_t tcp_fin = 0;
            uint64_t tcp_psh_ack = 0;
            uint64_t tcp_non_psh_ack = 0;
            uint64_t tcp_pure_ack = 0;
            uint64_t tcp_dup_ack = 0;
            uint64_t tcp_flag_invalid = 0;
            for(int q = 0; q < ZCAP_HASH_Q_SIZE; ++ q)
            {
                tcp_total += captor->alz[q].stats.tcp.total;
                _tcp_ack_total += captor->alz[q].stats.tcp._ack_total;
                tcp_pure_syn += captor->alz[q].stats.tcp.pure_syn;
                tcp_syn_ack += captor->alz[q].stats.tcp.syn_ack;
                tcp_rst += captor->alz[q].stats.tcp.rst;
                tcp_fin += captor->alz[q].stats.tcp.fin;
                tcp_psh_ack += captor->alz[q].stats.tcp.psh_ack;
                tcp_non_psh_ack += captor->alz[q].stats.tcp.non_psh_ack;
                tcp_pure_ack += captor->alz[q].stats.tcp.pure_ack;
                tcp_dup_ack += captor->alz[q].stats.tcp.dup_ack;
                tcp_flag_invalid += captor->alz[q].stats.tcp.flag_invalid;
            }
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), 
                "\n"
                "-----------------------------------------------------------------\n"
                "               TCP Flag Traffic Statistics                \n"
                "-----------------------------------------------------------------\n"
                "[ Sumary ]\n"
                "  Total TCP Count                                      %10u\n"
                "\n"
                "[ Connection Establishment (Handshake) ]\n"
                "  SYN       (Client request to open connection):       %10u\n"
                "  SYN-ACK   (Server acceptance of connection):         %10u\n"
                "\n"
                "[ Data Transfer Phase ]\n"
                "  PSH+ACK   (Immediate application delivery):          %10u\n"
                "  ACK       (Ack with payload, non inmediate):         %10u\n"
                "  Pure ACK  (Acknowledgment only, no payload):         %10u\n"
                "  Dup ACK   (Duplicate Ack, low qualitity connection): %10u\n"
                "                                      Dup ACK Ratio: %10g %%\n"
                "\n"
                "[ Connection Teardown & Errors ]\n"
                "  FIN       (Graceful connection termination):         %10u\n"
                "  RST       (Abrupt reset / refused connection):       %10u\n"
                "\n"
                "[ Security & Anomalies ]\n"
                "  Invalid   (Malformed flags / port scanning):         %10u\n"
                "-----------------------------------------------------------------\n",
                tcp_total,
                tcp_pure_syn, 
                tcp_syn_ack, 
                tcp_psh_ack, 
                tcp_non_psh_ack,
                tcp_pure_ack, 
                tcp_dup_ack,
                (double)tcp_dup_ack / _tcp_ack_total * 100.0,
                tcp_fin,
                tcp_rst, 
                tcp_flag_invalid
            );

            // 打印注册的过滤项
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), "\nUser Register Packet Filter:\n");
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), ZCAP_DUMP_PKT_TYPE_NUM_HEAD_FMT, "type", "count");
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), ZCAP_DUMP_PKT_TYPE_NUM_HEAD_FMT, "----", "-----");
            zcap_pkt_filter_t *filter = zcap_pkt_filter_hash_first(&captor->filters);
            while(filter)
            {
                snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), ZCAP_DUMP_PKT_TYPE_NUM_FMT, filter->name, ATOM_LOAD(&filter->match_cnt, MORDER_RELAXED));
                filter = zcap_pkt_filter_hash_next(&captor->filters, filter);
            }

            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), "********************************\n\n");

            captor = zcaptor_hash_next(&g_zcaptor_hash_head, captor);
        }
    }
    cli_printf(ctx, "%s", temp_buf);
}

void zcap_dump_pkt_cli_hook(cli_ctx_t *ctx, unsigned char argc, char *argv[])
{
    if(argc != 1)
    {
        cli_printf(ctx, "-zcap: Error param cnt\n");
        return;
    }

    int period = cli_param_parse_str_2_u32(argv[0]);
    if(-1 == period)
    {
        cli_printf(ctx, "-zcap: Error param: %s\n", argv[0]);
        return;
    }

    unsigned char expected = 0;
    if(ATOM_CMP_XCHG_WEAK(&g_zcap_dump_pkt_info, &expected, 1, MORDER_SEQ_SCT, MORDER_RELAXED))
    {
        sleep(period);
        ATOM_STORE(&g_zcap_dump_pkt_info, 0, MORDER_RELAXED);
    }
}

void zcap_dump_src_top_cli_hook(cli_ctx_t *ctx, unsigned char argc, char *argv[])
{
    char temp_buf[512] = {};
    ev_with_rdlock(&g_zcaptor_hash_rwlock)
    {
        zcap_t *captor = zcaptor_hash_first(&g_zcaptor_hash_head);
        while(captor)
        {
            zcap_src_top_heap_head_t heap = {};
            zcap_src_top_heap_init(&heap);
            // 加入堆排序
            ev_with_spinlock(&captor->top.lock)
            {
                zcap_top_src_entry_t *entry = zcap_src_top_hash_first(&captor->top.hashtable);
                while(entry)
                {
                    zcap_src_top_heap_add(&heap, entry);
                    entry = zcap_src_top_hash_next(&captor->top.hashtable, entry);
                }
            }
            // 打印一下
            int top = 0;
            zcap_top_src_entry_t *entry = NULL;
            snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf), 
                "\nTop [%d] of interface: %s\n", ZCAP_TOP_CNT, captor->if_name);
            while(top < ZCAP_TOP_CNT && (entry = zcap_src_top_heap_pop(&heap)))
            {
                snprintf(temp_buf + strlen(temp_buf), sizeof(temp_buf) - strlen(temp_buf),
                    "Top %d: %02x-%02x-%02x-%02x-%02x-%02x, cnt %d\n", top+1,
                    entry->smac[0], entry->smac[1], entry->smac[2], entry->smac[3], entry->smac[4], entry->smac[5], entry->cnt);
                ++ top;
            }

            zcap_src_top_heap_fini(&heap);
            captor = zcaptor_hash_next(&g_zcaptor_hash_head, captor);
        }
    }
    cli_printf(ctx, "%s\n", temp_buf);
}

error_code_e zcap_rx_http_cgi_cb(json_builder_t *jb)
{
    double eth0_rx_rate = 0;
    double wlan0_rx_rate = 0;
    unsigned long eth0_rx_size = 0;
    unsigned long wlan0_rx_size = 0;

    ev_with_rdlock(&g_zcaptor_hash_rwlock)
    {
        zcap_t *captor = zcaptor_hash_first(&g_zcaptor_hash_head);
        while(captor)
        {
            if(0 == strcmp(captor->if_name, "eth0"))
            {
                for(int q = 0; q < ZCAP_HASH_Q_SIZE; ++ q)
                    eth0_rx_size += captor->alz[q].stats.bytes;
                ev_with_spinlock(&captor->stats.lock)
                    eth0_rx_rate = captor->stats.Bps / 1000.0;
            }
            else if(0 == strcmp(captor->if_name, "wlan0"))
            {
                for(int q = 0; q < ZCAP_HASH_Q_SIZE; ++ q)
                    wlan0_rx_size += captor->alz[q].stats.bytes;
                ev_with_spinlock(&captor->stats.lock)
                    wlan0_rx_rate = captor->stats.Bps / 1000.0;
            }
            captor = zcaptor_hash_next(&g_zcaptor_hash_head, captor);
        }
    }

    json_object_start(jb);

    json_object_start_with_key(jb, "eth0");
    json_add_double(jb, "rx_rate", eth0_rx_rate);
    json_add_int(jb, "rx_size", eth0_rx_size);
    json_object_end(jb);

    json_object_start_with_key(jb, "wlan0");
    json_add_double(jb, "rx_rate", wlan0_rx_rate);
    json_add_int(jb, "rx_size", wlan0_rx_size);
    json_object_end(jb);

    json_object_end(jb);

    return ERR_NO_ERROR;
}

error_code_e zcap_sync_rx_http_cgi_cb(json_builder_t *jb)
{
    double eth0_rx_rate = 0;
    double wlan0_rx_rate = 0;
    unsigned long eth0_rx_size = 0;
    unsigned long wlan0_rx_size = 0;

    ev_with_rdlock(&g_zcaptor_hash_rwlock)
    {
        zcap_t *captor = zcaptor_hash_first(&g_zcaptor_hash_head);
        while(captor)
        {
            if(0 == strcmp(captor->if_name, "eth0"))
            {
                for(int q = 0; q < ZCAP_HASH_Q_SIZE; ++ q)
                    eth0_rx_size += captor->alz[q].stats.bytes;
                ev_with_spinlock(&captor->stats.lock)
                    eth0_rx_rate = captor->stats.Bps / 1000.0;
            }
            else if(0 == strcmp(captor->if_name, "wlan0"))
            {
                for(int q = 0; q < ZCAP_HASH_Q_SIZE; ++ q)
                    wlan0_rx_size += captor->alz[q].stats.bytes;
                ev_with_spinlock(&captor->stats.lock)
                    wlan0_rx_rate = captor->stats.Bps / 1000.0;
            }
            captor = zcaptor_hash_next(&g_zcaptor_hash_head, captor);
        }
    }

    json_object_start_with_key(jb, "eth0");
    json_add_double(jb, "rx_rate", eth0_rx_rate);
    json_add_int(jb, "rx_size", eth0_rx_size);
    json_object_end(jb);

    json_object_start_with_key(jb, "wlan0");
    json_add_double(jb, "rx_rate", wlan0_rx_rate);
    json_add_int(jb, "rx_size", wlan0_rx_size);
    json_object_end(jb);

    return ERR_NO_ERROR;
}

error_code_e zcap_stream_pie_cgi_cb(json_builder_t *jb)
{
    json_object_start(jb);

    // label
    json_array_start_with_key(jb, "labels");
    json_array_add_string(jb, "< 64 B");
    json_array_add_string(jb, "64~127 B");
    json_array_add_string(jb, "128~255 B");
    json_array_add_string(jb, "256~511 B");
    json_array_add_string(jb, "512~1023 B");
    json_array_add_string(jb, "1024~1517 B");
    json_array_add_string(jb, ">= 1518 B");
    json_array_end(jb);

    ev_with_rdlock(&g_zcaptor_hash_rwlock)
    {
        zcap_t *captor = zcaptor_hash_first(&g_zcaptor_hash_head);
        while(captor)
        {
            // 打印统计数据，收集per-thread stats
            uint64_t size_lt_64 = 0;
            uint64_t size_64_128 = 0;
            uint64_t size_128_256 = 0;
            uint64_t size_256_512 = 0;
            uint64_t size_512_1024 = 0;
            uint64_t size_1024_1518 = 0;
            uint64_t size_gt_1518 = 0;
            for(int q = 0; q < ZCAP_HASH_Q_SIZE; ++ q)
            {
                size_lt_64 += captor->alz[q].stats.size_lt_64;
                size_64_128 += captor->alz[q].stats.size_64_128;
                size_128_256 += captor->alz[q].stats.size_128_256;
                size_256_512 += captor->alz[q].stats.size_256_512;
                size_512_1024 += captor->alz[q].stats.size_512_1024;
                size_1024_1518 += captor->alz[q].stats.size_1024_1518;
                size_gt_1518 += captor->alz[q].stats.size_gt_1518;
            }

            json_array_start_with_key(jb, captor->if_name);
            json_array_add_int(jb, size_lt_64);
            json_array_add_int(jb, size_64_128);
            json_array_add_int(jb, size_128_256);
            json_array_add_int(jb, size_256_512);
            json_array_add_int(jb, size_512_1024);
            json_array_add_int(jb, size_1024_1518);
            json_array_add_int(jb, size_gt_1518);
            json_array_end(jb);

            captor = zcaptor_hash_next(&g_zcaptor_hash_head, captor);
        }
    }

    json_object_end(jb);

    return ERR_NO_ERROR;
}

error_code_e zcap_sync_stream_pie_cgi_cb(json_builder_t *jb)
{
    // label
    json_array_start_with_key(jb, "labels");
    json_array_add_string(jb, "< 64 B");
    json_array_add_string(jb, "64~127 B");
    json_array_add_string(jb, "128~255 B");
    json_array_add_string(jb, "256~511 B");
    json_array_add_string(jb, "512~1023 B");
    json_array_add_string(jb, "1024~1517 B");
    json_array_add_string(jb, ">= 1518 B");
    json_array_end(jb);

    ev_with_rdlock(&g_zcaptor_hash_rwlock)
    {
        zcap_t *captor = zcaptor_hash_first(&g_zcaptor_hash_head);
        while(captor)
        {
            // 打印统计数据，收集per-thread stats
            uint64_t size_lt_64 = 0;
            uint64_t size_64_128 = 0;
            uint64_t size_128_256 = 0;
            uint64_t size_256_512 = 0;
            uint64_t size_512_1024 = 0;
            uint64_t size_1024_1518 = 0;
            uint64_t size_gt_1518 = 0;
            for(int q = 0; q < ZCAP_HASH_Q_SIZE; ++ q)
            {
                size_lt_64 += captor->alz[q].stats.size_lt_64;
                size_64_128 += captor->alz[q].stats.size_64_128;
                size_128_256 += captor->alz[q].stats.size_128_256;
                size_256_512 += captor->alz[q].stats.size_256_512;
                size_512_1024 += captor->alz[q].stats.size_512_1024;
                size_1024_1518 += captor->alz[q].stats.size_1024_1518;
                size_gt_1518 += captor->alz[q].stats.size_gt_1518;
            }

            json_array_start_with_key(jb, captor->if_name);
            json_array_add_int(jb, size_lt_64);
            json_array_add_int(jb, size_64_128);
            json_array_add_int(jb, size_128_256);
            json_array_add_int(jb, size_256_512);
            json_array_add_int(jb, size_512_1024);
            json_array_add_int(jb, size_1024_1518);
            json_array_add_int(jb, size_gt_1518);
            json_array_end(jb);

            captor = zcaptor_hash_next(&g_zcaptor_hash_head, captor);
        }
    }

    return ERR_NO_ERROR;
}

error_code_e zcap_protocol_bar_cgi_cb(json_builder_t *jb)
{
    json_object_start(jb);

    // label数组
    json_array_start_with_key(jb, "labels");
    json_array_add_string(jb, "arp");
    json_array_add_string(jb, "icmp");
    json_array_add_string(jb, "miscc_web");
    json_array_add_string(jb, "ssh");
    json_array_add_string(jb, "dns");
    json_array_add_string(jb, "dhcp");
    json_array_add_string(jb, "tcp_other");
    json_array_add_string(jb, "udp_other");
    json_array_add_string(jb, "others");
    json_array_end(jb);

    ev_with_rdlock(&g_zcaptor_hash_rwlock)
    {
        zcap_t *captor = zcaptor_hash_first(&g_zcaptor_hash_head);
        while(captor)
        {
            uint64_t arp = 0;
            uint64_t icmp = 0;
            uint64_t miscc_web = 0;
            uint64_t ssh = 0;
            uint64_t dns = 0;
            uint64_t dhcp = 0;
            uint64_t tcp_other = 0;
            uint64_t udp_other = 0;
            uint64_t others = 0;
            for(int q = 0; q < ZCAP_HASH_Q_SIZE; ++ q)
            {
                arp += captor->alz[q].stats.arp;
                icmp += captor->alz[q].stats.icmp;
                miscc_web += captor->alz[q].stats.miscc_web;
                ssh += captor->alz[q].stats.ssh;
                dns += captor->alz[q].stats.dns;
                dhcp += captor->alz[q].stats.dhcp;
                tcp_other += captor->alz[q].stats.tcp_other;
                udp_other += captor->alz[q].stats.udp_other;
                others += captor->alz[q].stats.others;
            }

            json_array_start_with_key(jb, captor->if_name);
            json_array_add_int(jb, arp);
            json_array_add_int(jb, icmp);
            json_array_add_int(jb, miscc_web);
            json_array_add_int(jb, ssh);
            json_array_add_int(jb, dns);
            json_array_add_int(jb, dhcp);
            json_array_add_int(jb, tcp_other);
            json_array_add_int(jb, udp_other);
            json_array_add_int(jb, others);
            json_array_end(jb);

            captor = zcaptor_hash_next(&g_zcaptor_hash_head, captor);
        }
    }

    json_object_end(jb);

    return ERR_NO_ERROR;
}

error_code_e zcap_sync_protocol_bar_cgi_cb(json_builder_t *jb)
{
    // label数组
    json_array_start_with_key(jb, "labels");
    json_array_add_string(jb, "arp");
    json_array_add_string(jb, "icmp");
    json_array_add_string(jb, "miscc_web");
    json_array_add_string(jb, "ssh");
    json_array_add_string(jb, "dns");
    json_array_add_string(jb, "dhcp");
    json_array_add_string(jb, "tcp_other");
    json_array_add_string(jb, "udp_other");
    json_array_add_string(jb, "others");
    json_array_end(jb);

    ev_with_rdlock(&g_zcaptor_hash_rwlock)
    {
        zcap_t *captor = zcaptor_hash_first(&g_zcaptor_hash_head);
        while(captor)
        {
            uint64_t arp = 0;
            uint64_t icmp = 0;
            uint64_t miscc_web = 0;
            uint64_t ssh = 0;
            uint64_t dns = 0;
            uint64_t dhcp = 0;
            uint64_t tcp_other = 0;
            uint64_t udp_other = 0;
            uint64_t others = 0;
            for(int q = 0; q < ZCAP_HASH_Q_SIZE; ++ q)
            {
                arp += captor->alz[q].stats.arp;
                icmp += captor->alz[q].stats.icmp;
                miscc_web += captor->alz[q].stats.miscc_web;
                ssh += captor->alz[q].stats.ssh;
                dns += captor->alz[q].stats.dns;
                dhcp += captor->alz[q].stats.dhcp;
                tcp_other += captor->alz[q].stats.tcp_other;
                udp_other += captor->alz[q].stats.udp_other;
                others += captor->alz[q].stats.others;
            }

            json_array_start_with_key(jb, captor->if_name);
            json_array_add_int(jb, arp);
            json_array_add_int(jb, icmp);
            json_array_add_int(jb, miscc_web);
            json_array_add_int(jb, ssh);
            json_array_add_int(jb, dns);
            json_array_add_int(jb, dhcp);
            json_array_add_int(jb, tcp_other);
            json_array_add_int(jb, udp_other);
            json_array_add_int(jb, others);
            json_array_end(jb);

            captor = zcaptor_hash_next(&g_zcaptor_hash_head, captor);
        }
    }

    return ERR_NO_ERROR;
}

static void* _zcap_rate_cal_wf(void *args)
{
    const char *name = "zcap_rate_cal";

    prctl(PR_SET_NAME, name);
    swdg_register(name, ZCAP_CAL_RATE_INTERVAL * 3);

    ATOM_STORE(&g_zcap_rx_rate_cal_tid_run, 1, MORDER_RELAXED);
    while(ATOM_LOAD(&g_zcap_rx_rate_cal_tid_run, MORDER_RELAXED))
    {
        ev_with_rdlock(&g_zcaptor_hash_rwlock)
        {
            swdg_feed();
            zcap_t *captor = zcaptor_hash_first(&g_zcaptor_hash_head);
            while(captor)
            {
                // 统计当前总数
                uint64_t curr_counts = 0;
                uint64_t curr_bytes = 0;
                for(int q = 0; q < ZCAP_HASH_Q_SIZE; ++ q)
                {
                    curr_counts += captor->alz[q].stats.counts;
                    curr_bytes += captor->alz[q].stats.bytes;
                }
                // 计算上次差值，更新
                ev_with_spinlock(&captor->stats.lock)
                {
                    uint64_t delta_counts = curr_counts - captor->stats.last_counts;
                    uint64_t delta_bytes = curr_bytes - captor->stats.last_bytes;
                    captor->stats.last_counts = curr_counts;
                    captor->stats.last_bytes = curr_bytes;
                    captor->stats.pps = (double)delta_counts / (ZCAP_CAL_RATE_INTERVAL / 1000.0);
                    captor->stats.Bps = (double)delta_bytes / (ZCAP_CAL_RATE_INTERVAL / 1000.0);
                }

                captor = zcaptor_hash_next(&g_zcaptor_hash_head, captor);
            }
        }
        usleep(ZCAP_CAL_RATE_INTERVAL * 1000);
    }

    swdg_deregister(name);
}