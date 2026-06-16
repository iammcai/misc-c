/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    zcap.c
 * @brief   抓包实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-06-11
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-06-11 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <sys/socket.h>         // for socket
#include <linux/if_packet.h>    // for sockaddr_ll, PACKETV3
#include <linux/if_ether.h>     // for ETH_P_ALL
#include <netinet/in.h>         // for htons
#include <net/if.h>             // for ifreq
#include <arpa/inet.h>
#include <sys/ioctl.h>          // for ioctl
#include <errno.h>              // for errno
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>           // for mmap
#include <sys/epoll.h>          // for epoll

#include "zcap.h"
#include "plat/debug.h"
#include "cli/cli.h"
#include "event/ev_thread.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 是否打印报文具体内容
#define DUMP_PKT_DETAIL (0)

// 一个块的大小，mmap要求页对齐，此处设置页*1024
#define BLOCK_SIZE      (sysconf(_SC_PAGESIZE)*1024)
// 块的数量，一共64*4MB=256MB映射
#define BLOCK_NR        (64)
// 帧数量
#define FRAME_NR        (BLOCK_SIZE*BLOCK_NR/FRAME_SIZE)

// 内存池节点数量，开启MP_DETAIL_DUMP观察调整
#define ZCAP_ST_MP_NODE_COUNT   (512)

// 抓包epoll超时 200ms
#define EPOLL_TIMEOUT   (200)

#define err_str         strerror(errno)

#define ZCAP_CAL_RATE_INTERVAL  (1000)  // 计算速率间隔，1s一次

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// 内存池，用于rx_t和alz_t之间交互报文
declare_mem_type_fixed(zcap_pkt, sizeof(zcap_packet_t), ZCAP_ST_MP_NODE_COUNT)

/* ========================================================================== */
/*                        Static Function Prototypes                          */
/* ========================================================================== */

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
 * @brief       构造初始化全局hashtable
 */
static attr_force_inline void g_zcaptor_hash_init() attr_ctor(CTOR_PRIO_HIGH);
static attr_force_inline void g_zcaptor_hash_init()
{
    zcaptor_hash_init(&g_zcaptor_hash_head);
}

/**
 * @brief       ctor init zcap module
 * 
 * @note        构造函数，全局初始化
 */
static attr_force_inline void zcap_global_init() attr_ctor(CTOR_PRIO_MID);

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

    // 取消映射
    if(captor->ring_buffer)
        munmap(captor->ring_buffer, BLOCK_SIZE*BLOCK_NR);
    captor->ring_len = 0;

    memset(captor->if_mac, 0, MAC_ADDR_SIZE);
    captor->if_index = -1;
    // 关闭socket
    if(captor->sock_fd != -1)
        close(captor->sock_fd);

    ATOM_STORE(&captor->ready, 0, MORDER_RELEASE);

    // 如果抓包正在运行，那么结束它
    unsigned char expected = 1;
    if(ATOM_CMP_XCHG_WEAK(&captor->running, &expected, 0, MORDER_SEQ_SCT, MORDER_RELAXED))
        pthread_join(captor->rx_t, NULL);   // 等待结束

    // 如果解析报文正在运行，那么结束它
    expected = 1;
    if(ATOM_CMP_XCHG_WEAK(&captor->analyzing, &expected, 0, MORDER_SEQ_SCT, MORDER_RELAXED))
    {
        pthread_join(captor->alz_t, NULL);
    }

    // 销毁消息队列
    zcap_packet_spsc_atom_queue_fini(&captor->aq_head);
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

    // 内存池初始化，补充节点
    mp_fixed_init(zcap_pkt)
    mp_fixed_supply(zcap_pkt)

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

                // 构建消息，将报文发给解析线程
                zcap_packet_t *packet = mp_fixed_node_get(zcap_pkt);
                if(!packet)
                {
                    dbg_error("no mem for packet node, packet loss");
                    continue;
                }
                memcpy(packet->packet, pkt_data, pkt_len < FRAME_SIZE ? pkt_len : FRAME_SIZE);  // 复制内容
                packet->len = pkt_len;
                zcap_packet_spsc_atom_queue_push(&captor->aq_head, packet);     // 入队
            }

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
 * @brief       analyze a packet
 * 
 * @param[in]   captor  - captor
 * @param[in]   packet  - packet info
 * 
 * @note        解析单个报文
 */
static void _zcap_analyze_a_pkt(zcap_t *captor, zcap_packet_t *packet)
{
    if(!captor || !packet)
        return;

    if(packet->len < sizeof(struct ethhdr))      // error pkt
        return;

    const struct ethhdr *l2 = (struct ethhdr*)packet->packet;

    if(!memcmp(l2->h_source, captor->if_mac, MAC_ADDR_SIZE))
        return;

    // 可选，打印报文
#if DUMP_PKT_DETAIL
    dbg_major("analyze receive pkt, len %d", packet->len);
    int i = 0;
    for(i = 0; i < packet->len; ++ i)
    {
        safe_printf("%02x ", packet->packet[i]);
        if((i+1)%32 == 0)
            safe_printf("\n");
        else if((i+1)%16 == 0)
            safe_printf("  ");
    }
    safe_printf("\n");
#endif

    if(ATOM_LOAD(&g_zcap_dump_pkt_info, MORDER_RELAXED))
        safe_printf("%-5s: len %-4d: src:%02x:%02x:%02x:%02x:%02x:%02x, type %s\n",
            captor->if_name, packet->len,
            l2->h_source[0], l2->h_source[1], l2->h_source[2], l2->h_source[3], l2->h_source[4], l2->h_source[5],
            _zcap_get_ethertype_name(ntohs(l2->h_proto))
        );

    // 更新统计数据
    zcap_stat_t *stat = &captor->stat;
    ev_with_mutex(&stat->mtx)
    {
        stat->count += 1;
        stat->size += packet->len;
    }
}

/**
 * @brief       analyze pkts
 * 
 * @param[in]   args    - captor
 * 
 * @retval      NULL
 */
static void* _zcap_analyze_routine(void* args)
{
    zcap_t *captor = (zcap_t*)args;

    // 设置启动标志
    int expected = 0;
    if(!ATOM_CMP_XCHG_WEAK(&captor->analyzing, &expected, 1, MORDER_SEQ_SCT, MORDER_RELAXED))
        return NULL;

    memset(&captor->stat, 0, sizeof(zcap_stat_t));   // 清空统计
    // 初始化内存池，不用补充节点
    mp_fixed_init(zcap_pkt)

    while(ATOM_LOAD(&captor->analyzing, MORDER_ACQUIRE))    // 主循环
    {
        zcap_packet_t *packet = zcap_packet_spsc_atom_queue_pop(&captor->aq_head);
        if(!packet)
        {
            usleep(1000);    // sleep 1ms，这里休眠关系不大，报文来了仍在队列里
            continue;
        }
        else
        {
            _zcap_analyze_a_pkt(captor, packet);
            mp_fixed_node_put(packet);
        }
        
    }

    return NULL;
}

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void _zcap_init(zcap_t *captor)
{
    // 加到哈希表
    zcaptor_hash_add(&g_zcaptor_hash_head, captor);

    // rx_t和alz_t消息队列初始化
    zcap_packet_spsc_atom_queue_init(&captor->aq_head);

    // 创建解析报文线程
    if(-1 == pthread_create(&captor->alz_t, NULL, _zcap_analyze_routine, captor))
    {
        dbg_error("create analyze thread fail");
        goto error;
    }

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
        dbg_major("capture rx thread ok");
    }
}

static inline void zcap_global_init()
{
    // 注册cli
    cli_register("show zcaptor", "dump info of zcaptor", NULL, zcap_dump_captor_cli_hook);
    cli_param_t dump_pkt_param[] = {
        {.required = 1, .short_name = 't', .type = PARAM_VALUE, .help = "means dump period(s)"},
    };
    cli_register("zcaptor dump packet", "dump info of rx packet during a preiod", dump_pkt_param, zcap_dump_pkt_cli_hook);

    // 启动zcap cal rate线程
    ev_thd_run(zcap_rate_cal);
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
        safe_printf("runing flag: %d, analyze flag %d\n", 
            ATOM_LOAD(&captor->running, MORDER_ACQUIRE),
            ATOM_LOAD(&captor->analyzing, MORDER_ACQUIRE)
        );

        ev_with_mutex(&captor->stat.mtx)
        {
            safe_printf("rx_count: %u, %.3f pps\n", captor->stat.count, captor->stat.rate_pps);
            safe_printf("rx_size: %.3f KB, %.3f KBps\n",
                (double)captor->stat.size/1024,
                captor->stat.rate_Bps/1024
            );
        }
        safe_printf("********************************\n\n");

        captor = zcaptor_hash_next(&g_zcaptor_hash_head, captor);
    }

    return NULL;
}