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

#include "zcap/zcap.h"
#include "plat/debug.h"
#include "cli/cli.h"
#include "event/ev_thread.h"
#include "event/ev_loop.h"
#include "mp/mp_slab.h"

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

// 抓包epoll超时 200ms
#define EPOLL_TIMEOUT   (200)

#define err_str         strerror(errno)

#define ZCAP_CAL_RATE_INTERVAL  (1000)  // 计算速率间隔，1s一次

#define ZCAP_ANALYZE_PKTS_ONCE  (16)    // 每次最多处理16个报文，避免reactor队头阻塞

#define ZCAP_HASH_Q_MASK        (ZCAP_HASH_Q_SIZE - 1)  // hash分片处理报文的队列掩码

#define ZCAP_DUMP_PKT_TYPE_NUM_HEAD_FMT     "%-8s%-8s\n"
#define ZCAP_DUMP_PKT_TYPE_NUM_FMT          "%-8s%-8u\n"  // 打印各类型报文数量的格式，type num
#define ZCAP_DUMM_PKT_INFO_FMT              "if-%s,len-%-4d: %s\n"      // 打印报文信息格式

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 内存池，用于rx_t和alz_t之间交互报文
declare_mem_type_slab(zcap_pkt)

// 封装一下，传递给alz线程入口
typedef struct{
    zcap_t *cap;
    unsigned char index;
}alz_arg_packed;

/* ========================================================================== */
/*                        Static Function Prototypes                          */
/* ========================================================================== */

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
        flow_key->protocol = (uint8_t)(ether_type && 0xff);
        memcpy(&flow_key->src_ip, eth->h_source, 4);
    }
}

/**
 * @brief       cal rate thread work func
 * 
 * @note        遍历zcaptor哈希表，更新速率
 */
static void _zcap_rate_cal_wf(void *args);

/* ========================================================================== */
/*                          Global/Static Variables                           */
/* ========================================================================== */

// 全局哈希表，存放所有zcaptor
static zcaptor_hash_head_t g_zcaptor_hash_head = {};
// 全局事件线程，用于统计速率统计
declare_ev_thd(zcap_rate_cal, _zcap_rate_cal_wf, NULL, ZCAP_CAL_RATE_INTERVAL)

// 控制是否打印报文简要信息
static ATOMIC_UINT8_T g_zcap_dump_pkt_info = 0;

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

static void _zcap_rate_cal_wf(void *args)
{
    zcap_t *captor = zcaptor_hash_first(&g_zcaptor_hash_head);
    while(captor)
    {
        if(ATOM_LOAD(&captor->running, MORDER_ACQUIRE))     // 仅统计运行中的
        {
            zcap_stat_t *stat = &captor->stat;
            ev_with_mutex(&stat->mtx)
            {
                stat->last_count = stat->curr_count;
                stat->last_size = stat->curr_size;
                stat->curr_count = stat->count;
                stat->curr_size = stat->size;

                double sec = (double)ZCAP_CAL_RATE_INTERVAL / 1000;
                
                stat->rate_Bps = (double)(stat->curr_size - stat->last_size) / sec;
                stat->rate_pps = (double)(stat->curr_count - stat->last_count) / sec;
            }
        }
        captor = zcaptor_hash_next(&g_zcaptor_hash_head, captor);       // 继续下一个
    }
}

/**
 * @brief       cli hook for: show zcaptor
 * 
 * @note        打印zcaptor信息
 */
static void* zcap_dump_captor_cli_hook(unsigned char argc, char *argv[]);

static attr_force_inline void* zcap_dump_pkt_cli_hook(unsigned char argc, char *argv[])
{
    if(argc != 1)
    {
        safe_printf("-zcap: Error param cnt\n");
        return NULL;
    }

    int period = cli_param_parse_str_2_u32(argv[0]);
    if(-1 == period)
    {
        safe_printf("-zcap: Error param: %s\n", argv[0]);
        return NULL;
    }

    unsigned char expected = 0;
    if(ATOM_CMP_XCHG_WEAK(&g_zcap_dump_pkt_info, &expected, 1, MORDER_SEQ_SCT, MORDER_RELAXED))
    {
        sleep(period);
        ATOM_STORE(&g_zcap_dump_pkt_info, 0, MORDER_RELAXED);
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
    zcaptor_hash_del(&g_zcaptor_hash_head, captor);

    // 如果抓包正在运行，那么结束它
    unsigned char expected = 1;
    if(ATOM_CMP_XCHG_WEAK(&captor->running, &expected, 0, MORDER_SEQ_SCT, MORDER_RELAXED))
        pthread_join(captor->rx_t, NULL);   // 等待结束

    // 销毁消息队列
    zcap_packet_spsc_atom_queue_fini(&captor->aq_head);

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

    dbg_major("start capture rx thread");

    unsigned int block_idx = 0;     // 环形缓冲区下标
    while(ATOM_LOAD(&captor->running, MORDER_ACQUIRE))  // 主循环，检查running标志
    {
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
                zcap_packet_t *packet = mp_slab_node_get(zcap_pkt, sizeof(zcap_packet_t)+pkt_len);
                if(!packet)
                {
                    dbg_error("no mem for packet node, packet loss");
                    continue;
                }
                memset(packet, 0, sizeof(zcap_packet_t)+pkt_len);
                memcpy(packet->packet, pkt_data, pkt_len);  // 复制内容
                packet->len = pkt_len;
                // 预提取五元组信息，而不是在reactor cb中进行
                _zcap_packet_flow_key_extract(packet);
                zcap_packet_spsc_atom_queue_push(&captor->aq_head, packet);     // 入队
            }

            // 通知解析
            eventfd_write_one(captor->event_fd);

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
 * @brief       cb for zcap eventfd In event loop
 * 
 * @note        收block包后，通知进行分析的回调
 */
static void _zcap_analyze_el_cb(int fd, void *args)
{
    zcap_t *captor = (zcap_t*)args;

    // 从spsc队列中拿报文，计算hash分发给解析线程
    int pkt_nums = 0;
    zcap_packet_t *packet;
    while(packet = zcap_packet_spsc_atom_queue_pop(&captor->aq_head))
    {
        pkthdr_l2_t *l2 = (pkthdr_l2_t*)(packet->packet);

        // 根据flow key计算hash
        unsigned int hash = type_hash_jhash(&packet->flow_key, sizeof(zcap_pakcet_flow_key_t), 0);
        hash &= ZCAP_HASH_Q_MASK;

        // 发给持有对应HashQ的线程处理
        zcap_packet_spsc_atom_queue_push(&captor->alz[hash].alz_aq_head, packet);
        ev_sem_post(&captor->alz[hash].alz_sem);

        ++ pkt_nums;
        if(pkt_nums >= ZCAP_ANALYZE_PKTS_ONCE)
            break;
    }

    // 超出一次处理，自写一次eventfd，以便再次cb来消费报文
    if(pkt_nums >= ZCAP_ANALYZE_PKTS_ONCE)
        eventfd_write_one(captor->event_fd);
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
    uint32_t len = packet->len;
    const struct ethhdr *eth = (const struct ethhdr*)packet->packet;    // l2头部
    memcpy(info->mac_da, eth->h_dest, MAC_ADDR_SIZE);
    memcpy(info->mac_sa, eth->h_source, MAC_ADDR_SIZE);
    info->ether_type = ntohs(eth->h_proto);

    uint32_t offset = sizeof(struct ethhdr);    // 偏移，用来计算下标是否越界

    // 暂时不考虑vlan
    if(ETH_P_IP == info->ether_type)
    {
        const struct iphdr *ip = (const struct iphdr*)((uint8_t*)packet->packet + offset);  // l3头部
        uint32_t ip_hdr_len = ip->ihl * 4;      // l3的长度，以4B为单位，包含头部

        uint32_t ip_da_h = ntohl(ip->daddr);
        uint32_t ip_sa_h = ntohl(ip->saddr);

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
static int _zcap_pkt_filed_match(zcap_pkt_field_t *field, zcap_pkt_info_t *info)
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
    zcap_stat_t *stat = &captor->stat;
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

    ev_with_mutex(&stat->mtx)
    {
        // 五元组信息统计
        if(packet->flow_key.err_pkt)
            ATOM_FETCH_ADD(&stat->err, 1, MORDER_ACQ_REL);

        ATOM_FETCH_ADD(&stat->count, 1, MORDER_ACQ_REL);
        ATOM_FETCH_ADD(&stat->size, packet->len, MORDER_ACQ_REL);
    }

    // 提取过滤器所需要的信息
    zcap_pkt_info_t packet_info = {};
    _zcap_pkt_info_extract(packet, &packet_info);

    // 遍历注册的过滤条件，执行回调
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

    mp_free(args, sizeof(alz_arg_packed));   // 释放入参

    while(ATOM_LOAD(&captor->running, MORDER_ACQUIRE))
    {
        // 先尝试批量 drain 队列
        int processed = 0;
        while ((packet = zcap_packet_spsc_atom_queue_pop(&captor->alz[i].alz_aq_head)))
        {
            pkthdr_l2_t *l2 = (pkthdr_l2_t*)(packet->packet);
            _zcap_analyze_a_packet(captor, packet);
            mp_slab_node_put(packet);
            processed++;
        }

        if (processed == 0)
            ev_sem_wait(&captor->alz[i].alz_sem);
    }

    return NULL;
}

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void zcap_module_init()
{
    zcaptor_hash_init(&g_zcaptor_hash_head);

    // mem type attr注册
    mem_type_attr_register(zcap_pkt);

    // 注册cli
    cli_register("show zcaptor", "dump info of zcaptor", NULL, zcap_dump_captor_cli_hook);
    cli_param_t dump_pkt_param[] = {
        {.required = 1, .short_name = 't', .type = PARAM_VALUE, .help = "means dump period(s)"},
    };
    cli_register("zcaptor dump packet", "dump info of rx packet during a preiod", dump_pkt_param, zcap_dump_pkt_cli_hook);

    // 启动zcap cal rate线程
    ev_thd_register(zcap_rate_cal);
    ev_thd_run(zcap_rate_cal);

    dbg_major("zcap module init ok");
}

void _zcap_init(zcap_t *captor)
{
    // 加到哈希表
    zcaptor_hash_add(&g_zcaptor_hash_head, captor);

    // 初始化过滤哈希表
    zcap_pkt_filter_hash_init(&captor->filters);

    // rx_t、alz_t消息队列初始化
    zcap_packet_spsc_atom_queue_init(&captor->aq_head);
    unsigned char i = 0;
    for(; i < ZCAP_HASH_Q_SIZE; ++ i)
    {
        zcap_packet_spsc_atom_queue_init(&(captor->alz[i].alz_aq_head));
        ev_sem_init(&captor->alz[i].alz_sem);
    }

    // 注册eventfd到ev_loop，用于通知进行analyze
    captor->event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    event_loop_register_file_event_eventfd(captor->event_fd, EL_FILE_EVENT_READABLE, _zcap_analyze_el_cb, captor)

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

void _zcap_register_pkt_filter(const char *if_name, const char *filter_name, zcap_pkt_filter_match_cb cb, void *args)
{
    assert(if_name && filter_name && cb);

    // 查找zcaptor
    zcap_t captor_key = {.if_name = if_name};
    zcap_t *zcaptor = zcaptor_hash_find(&g_zcaptor_hash_head, &captor_key);
    assert(zcaptor);

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
    zcap_t *zcaptor = zcaptor_hash_find(&g_zcaptor_hash_head, &captor_key);
    assert(zcaptor);

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

static void* zcap_dump_captor_cli_hook(unsigned char argc, char *argv[])
{
    zcap_t *captor = zcaptor_hash_first(&g_zcaptor_hash_head);

    while(captor)
    {
        safe_printf("\n********************************\n");
        safe_printf("if_name: %s\nif_index: %d\nif_mac: %02x-%02x-%02x-%02x-%02x-%02x\n", 
            captor->if_name, captor->if_index,
            captor->if_mac[0],captor->if_mac[1],captor->if_mac[2],
            captor->if_mac[3],captor->if_mac[4],captor->if_mac[5]
        );
        safe_printf("runing flag: %d\n\n", ATOM_LOAD(&captor->running, MORDER_ACQUIRE));

        ev_with_mutex(&captor->stat.mtx)
        {
            safe_printf("rx_count: %u, %.3f pps\n", captor->stat.count, captor->stat.rate_pps);
            safe_printf("rx_size: %.3f KB, %.3f KBps\n\n",
                (double)captor->stat.size/1024,
                captor->stat.rate_Bps/1024
            );

            safe_printf(ZCAP_DUMP_PKT_TYPE_NUM_HEAD_FMT, "type", "count");
            safe_printf(ZCAP_DUMP_PKT_TYPE_NUM_HEAD_FMT, "----", "-----");
            safe_printf(ZCAP_DUMP_PKT_TYPE_NUM_FMT, "ERROR", captor->stat.err);
        }
        
        // 打印注册的过滤项
        zcap_pkt_filter_t *filter = zcap_pkt_filter_hash_first(&captor->filters);
        while(filter)
        {
            safe_printf(ZCAP_DUMP_PKT_TYPE_NUM_FMT, filter->name, ATOM_LOAD(&filter->match_cnt, MORDER_RELAXED));
            filter = zcap_pkt_filter_hash_next(&captor->filters, filter);
        }

        safe_printf("********************************\n\n");

        captor = zcaptor_hash_next(&g_zcaptor_hash_head, captor);
    }

    return NULL;
}