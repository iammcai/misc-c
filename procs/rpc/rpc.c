/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    rpc.h
 * @brief   RPC实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-09-29
 * @version 1.0
 *
 * @note    整体为星型架构，一个RPC进程负责路由转发
 *          rpc broker  --- core proc
 *                      --- test proc
 *                      --- ...
 *          例如core调用test进程结构，需要先发给rpc broker处理，再rpc broker发给test进程
 *
 *          IPC使用unix stream socket，rpc作为receiver
 * 
 *          rpc通过epoll管理socket，使用ET+nonblocking
 * 
 *          rpc buffer格式：tlv, type(15B)-len(1B)-val
 *
 * @history
 *   1.0 | 2026-09-29 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/errno.h>
#include <sys/prctl.h>
#include <sys/select.h>

#include "rpc/rpc.h"
#include "ipc/uss/uss.h"
#include "plat/atom.h"
#include "ipc/shm/shm.h"
#include "fifo/fifo.h"
#include "mp/mp.h"
#include "type/type_hash.h"
#include "event/ev_lock.h"
#include "event/ev_sem.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// rpc调试打印开关
#define RPC_DEBUG_FLAG          (0)

// epoll监听最大数量
#define RPC_EPOLL_LISTEN_CNT    (PID_CNT * 2)

// epoll等待时间
#define RPC_EPOLL_WAIT      (2000)

// rpc func name长度
#define RPC_FUNC_NAME_SIZE  (32)

// 单次读取rpc buffer长度
#define RPC_READ_ONCE_SIZE  (2 << 10)
// buffer长度，必须2次幂
#define RPC_FIFO_SIZE               (RPC_READ_ONCE_SIZE << 3)
// 掩码
#define RPC_FIFO_MASK               (RPC_FIFO_SIZE - 1)

// 获取新的seq
#define PRC_SYNC_SEQ_NEW            ATOM_FETCH_ADD(&g_rpc_sync_seq_id, 1, MORDER_RELAXED)

// 同步调用池大小，最大同时支持同步调用个数
#define RPC_SYNC_POOL_SIZE          (64)

// 同步调用永久等待
#define RPC_SYNC_CALL_WAIT_FOREVER  (0)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// rpc进程uss管理
typedef struct{
    uss_mgnt_t *listener;
    uss_mgnt_t *accepter;       // accept后返回的新的uss
}rpc_uss_mgnt_t;

// TODO: rpc用户输入参数定义
struct rpc_in_param_s{
    int reserved;
};

// rpc头部结构定义
struct rpc_header_s{
    proc_id_e src;      // 调用方pid
    proc_id_e dest;     // 目的pid
    char func[RPC_FUNC_NAME_SIZE];          // 函数名
    int sync;           // 是否同步call
    uint32_t seq;       // 事务ID
    uint8_t req;        // 1 - 请求，0 - 响应
    size_t size;        // ctx长度
};

// 预定义hash结构，用于存储注册rpc服务函数
pre_declare_hash(rpc_server_func_entry)

// rpc注册函数结构定义
typedef struct{
    char name[RPC_FUNC_NAME_SIZE];
    rpc_server_func server_func;                // 服务函数
    rpc_server_func_entry_hash_item_t item;     // hash item
}rpc_server_func_entry_t;

// 同步调用上下文定义
typedef struct{
    proc_id_e src;
    proc_id_e dest;
    uint32_t seq;       // 序列号
    ev_sem_t sem;       // semphare
    uint8_t valid;      // 是否有效
    rpc_out_param_t out_param;      // 返回参数
}rpc_call_sync_ctx_t;

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       rpc proc epoll thread routine
 */
static void* _rpc_epoll_thd_routine(void *args);

/**
 * @brief       check if ready fd is new connection
 * 
 * @param[in]   fd
 * @param[out]  pid_out     - connect pid
 * 
 * @retval      0 - not new conn, 1 - y
 */
static attr_force_inline int _rpc_check_new_conn(int fd, proc_id_e *pid_out);

/**
 * @brief       handle new connection
 * 
 * @param[in]   pid     - pid
 * 
 * @note        rpc进程调用。accept新连接，创建accepter，加入到epoll监听
 */
static void _rpc_handle_new_conn(proc_id_e pid);

/**
 * @brief       handle msg
 * 
 * @param[in]   fd
 * 
 * @note        处理新消息，读取流式信息到fifo中待处理，避免粘包半包
 */
static void _rpc_handle_msg(int fd);

/**
 * @brief       handle rpc fifo msg
 * 
 * @note        处理fifo中缓存的rpc消息
 */
static void _rpc_fifo_handle(void);

/**
 * @brief       init rpc module for rpc process
 * 
 * @note        创建uss通道，作为receiver，需要处理accept
 */
static void _rpc_module_init_for_rpc(void);

/**
 * @brief       init rpc module for other process
 * 
 * @note        初始化uss通道，作为sender connect
 */
static void _rpc_module_init_for_others(void);

/**
 * @brief       exit rpc module for rpc process
 */
static void _rpc_module_exit_for_rpc(void);

/**
 * @brief       exit rpc module for non rpc proc
 */
static void _rpc_module_exit_for_others(void);

/**
 * @brief       non rpc proc handle select msg
 * 
 * @note        非rpc进程使用select，消息读取到fifo后再处理，避免粘包半包
 */
static void _rpc_other_proc_msg_handle();

/**
 * @brief       non rpc proc select thread
 * 
 * @note        非rpc进程使用select监听uss，单独线程执行
 */
static void* _rpc_select_routine(void *args);

/**
 * @brief       cmp func of rpc_server_func_entry_t
 */
static attr_pure_inline int _rpc_server_func_entry_cmp(rpc_server_func_entry_t *e1, rpc_server_func_entry_t *e2)
{
    pfm_ensure_ret(e1 && e1->name && e2 && e2->name, -1);
    return strcmp(e1->name, e2->name);
}

/**
 * @brief       hash func of rpc_server_func_entry_t
 */
static attr_pure_inline unsigned int _rpc_server_func_entry_hash(rpc_server_func_entry_t *e)
{
    pfm_ensure_ret(e && e->name, 0);
    return type_hash_jhash(e->name, strlen(e->name), 0);
}

// hash函数定义
declare_hash(rpc_server_func_entry, rpc_server_func_entry, rpc_server_func_entry_t, item, 1, 31, _rpc_server_func_entry_cmp, _rpc_server_func_entry_hash);

/**
 * @brief       add rpc sync ctx into pool
 * 
 * @param[in]   hdr     - rpc header
 * 
 * @retval      position to add
 */
static size_t _rpc_call_sync_ctx_pool_add(rpc_header_t *hdr);

/**
 * @brief       set rpc sync ctx into pool
 * 
 * @param[in]   hdr     - rpc header
 * @param[in]   out_param   - out param
 * 
 * @retval      error code
 * 
 * @note        收到同步响应，设置上下文池中对应信息，唤醒
 */
static error_code_e _rpc_call_sync_ctx_pool_set(rpc_header_t *hdr, rpc_out_param_t *out_param);

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// 全局socket path注册表
static const char* g_rpc_uss_path_table[PID_CNT] = {
    [PID_CORE] = "/tmp/core.sock",
    [PID_TEST] = "/tmp/test.sock",
};

// rpc进程使用，与其它进程的ipc通道receiver, accepter数组
static rpc_uss_mgnt_t g_rpc_uss_mgnt_table[PID_CNT] = {};

// 非rpc进程使用，与rpc之间的ipc通道sender
static uss_mgnt_t *g_rpc_uss_mgnt = NULL;

// rpc进程epoll线程id
static pthread_t g_rpc_epoll_tid;
// epoll fd
static int g_rpc_epoll_fd;
// 运行标志
static ATOMIC_UINT8_T g_rpc_epoll_thd_running = 0;

// 非rpc进程使用，select监听uss线程id
static pthread_t g_rpc_select_tid;
// 运行标志
static ATOMIC_UINT8_T g_rpc_select_thd_running = 0;

// rpc process运行标志
static shm_mgnt_t *rpc_process_running = NULL;

// rpc ring buffer
static fifo_t* g_rpc_fifo = NULL;

// non rpc proc ring buffer
static fifo_t* g_non_rpc_fifo = NULL;

// 所有进程的RPC服务函数注册表，进程独立，动态注册
static rpc_server_func_entry_hash_head_t g_rpc_server_func_entry_table = {};
// lock for g_rpc_server_func_entry_table
static ev_rwlock_t g_rpc_server_func_entry_table_rwlock = EV_RWLOCK_INITIALIZER;

// 全局事务序列号，进程独立
static ATOMIC_UINT32_T g_rpc_sync_seq_id = 0;
// 全局同步调用池，进程独立。调用线程阻塞sem，接收线程收到回复后解除sem阻塞
static rpc_call_sync_ctx_t g_rpc_call_sync_ctx_pool[RPC_SYNC_POOL_SIZE] = {};
// pool lock
static ev_mutex_t g_rpc_call_sync_ctx_pool_mutex = EV_MUTEX_INITIALIZER;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void rpc_server_func_register(const char *name, rpc_server_func func)
{
    pfm_ensure_ret_void(name && func);

    rpc_server_func_entry_t *entry = mp_calloc(1, sizeof(rpc_server_func_entry_t));
    pfm_ensure_ret_void(entry);
    entry->server_func = func;
    strncpy(entry->name, name, RPC_FUNC_NAME_SIZE - 1);

    ev_with_wrlock(&g_rpc_server_func_entry_table_rwlock)
        rpc_server_func_entry_hash_add(&g_rpc_server_func_entry_table, entry);
}

static inline int _rpc_check_new_conn(int fd, proc_id_e *pid_out)
{
    pfm_ensure_ret(pid_out, 0);
    for(proc_id_e pid = PID_CORE; pid < PID_CNT; ++ pid)
        if(fd == uss_mgnter_fd_get(g_rpc_uss_mgnt_table[pid].listener))
        {
            *pid_out = pid;
            return 1;
        }
    return 0;
}

static void _rpc_handle_new_conn(proc_id_e pid)
{
    pfm_ensure_ret_void(pid >= PID_CORE && pid < PID_CNT);

    // 每个pid只有一个连接，所以不while(1) accept
    g_rpc_uss_mgnt_table[pid].accepter = uss_mgnter_accept(g_rpc_uss_mgnt_table[pid].listener, 1);
    pfm_ensure_ret_void(g_rpc_uss_mgnt_table[pid].accepter);

    int fd = uss_mgnter_fd_get(g_rpc_uss_mgnt_table[pid].accepter);
    struct epoll_event ev = {
        .data.fd = fd,
        .events = EPOLLIN | EPOLLET,
    };
    pfm_ensure_ret_void(0 == epoll_ctl(g_rpc_epoll_fd, EPOLL_CTL_ADD, fd, &ev));

    dbg_major("handle new rpc connect for pid %d ok", pid);
}

static void _rpc_handle_msg(int fd)
{
    pfm_ensure_ret_void(fd >= 0);

    char buf[RPC_READ_ONCE_SIZE] = {};

    // IO需要读取完，不残留
    while(1)
    {
        // 根据fd找到对应的receiver
        uss_mgnt_t *receiver = NULL;
        for(proc_id_e pid = PID_CORE; pid < PID_CNT; ++ pid)
            if(uss_mgnter_fd_get(g_rpc_uss_mgnt_table[pid].accepter) == fd)
            {
                receiver = g_rpc_uss_mgnt_table[pid].accepter;
                break;
            }
        pfm_ensure_ret_void(receiver);

        // 读取到临时缓存
        memset(buf, 0, RPC_READ_ONCE_SIZE);
        ssize_t read_size = uss_recv(receiver, buf, RPC_READ_ONCE_SIZE);
        if(0 == read_size)
            break;
        else if(read_size < 0)
        {
            if(EINTR == errno)
                continue;
            else if(!(errno == EAGAIN || errno == EWOULDBLOCK))
            {
                dbg_error("uss_recv fail: %s", strerror(errno));
                break;
            }
        }

        // 复制到全局环形缓冲
        size_t remain = read_size;
        size_t offset = 0;
        while(remain)
        {
            size_t write_size = fifo_write(g_rpc_fifo, buf + offset, remain);
            _rpc_fifo_handle();
            offset += write_size;
            remain -= write_size;
        }

        // read长度小于一次最大值，说明已经读取完
        if(read_size < RPC_READ_ONCE_SIZE)
            break;
    }
}

static void _rpc_fifo_handle(void)
{
    while(1)
    {
        size_t data_size = fifo_get_used(g_rpc_fifo);       // 获取当前fifo数据长度
        if(data_size < sizeof(rpc_header_t))                // 不足一个rpc header，直接返回
            break;

        rpc_header_t header = {};
        fifo_read(g_rpc_fifo, (char*)&header, sizeof(rpc_header_t), 0);
        if(data_size < (sizeof(rpc_header_t) + header.size))            // 不足一个rpc header + ctx，直接返回
            break;

        // 正式处理
        size_t total_size = sizeof(rpc_header_t) + header.size;
        char *msg = mp_calloc(1, total_size);                       // TODO: pool

        fifo_read(g_rpc_fifo, msg, total_size, 1);
        rpc_header_t *p_header = (rpc_header_t*)msg;
#if RPC_DEBUG_FLAG
        dbg_always("src_pid %d, dest_pid %d, func_name %s, sync %d, seq %d, req %d, size %d", 
            p_header->src, p_header->dest, p_header->func, p_header->sync, p_header->seq, p_header->req, p_header->size);
#endif
        if((PROC_ID_IS_VALID(p_header->src) && PROC_ID_IS_VALID(p_header->dest)))
        {
            // 根据fd找到对应的receiver
            uss_mgnt_t *sender = g_rpc_uss_mgnt_table[p_header->dest].accepter;
            if(ERR_NO_ERROR != uss_send(sender, msg, total_size))
                dbg_error("uss_send fail()");
        }
        else
            dbg_error("error rpc msg");

        mp_free(msg, total_size);
    }
}

static void _rpc_module_init_for_rpc(void)
{
    for(proc_id_e pid = PID_CORE; pid < PID_CNT; ++ pid)
    {
        if(pid == PID_RPC)
            continue;
        g_rpc_uss_mgnt_table[pid].listener = uss_mgnter_create(g_rpc_uss_path_table[pid], 1, 1);
        if(!g_rpc_uss_mgnt_table[pid].listener)
            dbg_error("uss receiver create listener for proc id %d fail", pid);
    }
}

static void _rpc_module_init_for_others(void)
{
    proc_id_e pid = proc_id_get();

    g_rpc_uss_mgnt = uss_mgnter_create(g_rpc_uss_path_table[pid], 0, 1);
    if(unlikely(!g_rpc_uss_mgnt))
    {
        dbg_error("uss sender create for proc id %d fail", pid);
        return;
    }

    rpc_server_func_entry_hash_init(&g_rpc_server_func_entry_table);    // init hashtable
    
    // init ctx pool
    for(int i = 0; i < RPC_SYNC_POOL_SIZE; ++ i)
    {
        ev_sem_init(&g_rpc_call_sync_ctx_pool[i].sem);
        g_rpc_call_sync_ctx_pool[i].valid = 0;
    }

    // create fifo
    g_non_rpc_fifo = fifo_create(RPC_FIFO_SIZE);

    pthread_create(&g_rpc_select_tid, NULL, _rpc_select_routine, NULL);
}

static void _non_rpc_fifo_handle()
{
    while(1)
    {
        size_t data_size = fifo_get_used(g_non_rpc_fifo);

        if(data_size < sizeof(rpc_header_t))
        {
            break;
        }

        rpc_header_t hdr = {};
        fifo_read(g_non_rpc_fifo, (char*)&hdr, sizeof(rpc_header_t), 0);

        if(data_size < (sizeof(rpc_header_t) + hdr.size))
        {
            break;
        }

        size_t total_size = sizeof(rpc_header_t) + hdr.size;
        char *pkt = mp_calloc(1, total_size);           // TODO: mp
        size_t fifo_get_size = fifo_read(g_non_rpc_fifo, pkt, total_size, 1);
        if(fifo_get_size != total_size)
            dbg_error("fifo actually get %d, total %d", fifo_get_size, total_size);

        rpc_header_t *p_hdr = (rpc_header_t*)pkt;
        char *p_ctx = p_hdr->size ? pkt + sizeof(rpc_header_t) : NULL;      // 指向内容，可能无
#if RPC_DEBUG_FLAG
        dbg_always("handle select fifo msg: src_pid %d, dest_pid %d, func %s, sync %d, seq %d, req %d, size %d",
            p_hdr->src, p_hdr->dest, p_hdr->func, p_hdr->sync, p_hdr->seq, p_hdr->req, p_hdr->size);
#endif
        if(p_hdr->req)      // 处理请求
        {
            // 查服务函数表
            rpc_server_func_entry_t key = {}, *entry = NULL;
            strncpy(key.name, p_hdr->func, RPC_FUNC_NAME_SIZE - 1);
            ev_with_rdlock(&g_rpc_server_func_entry_table_rwlock)
                entry = rpc_server_func_entry_hash_find(&g_rpc_server_func_entry_table, &key);
            error_code_e ret;
            if(likely(entry))
            {
                ret = entry->server_func(p_hdr, NULL);         // 执行服务函数
#if RPC_DEBUG_FLAG
                dbg_always("========= call server func %s ok, ret %d", p_hdr->func, ret);
#endif
            }
            else
            {
                dbg_error("can't find server func for: %s", p_hdr->func);
                ret = ERR_RPC_SERVER_FUNC_NOT_EXIST;
            }

            if(!p_hdr->sync)   continue;        // async call到此结束
            else                                // sync call需要构造响应返回
            {
                // TODO: tlvs
                rpc_out_param_t out_param = {
                    .ret = ret,
                    .tlvs = NULL,
                    .tlvs_size = 0,
                };
                rpc_header_t resp_hdr = {
                    .dest = hdr.src,
                    .src = proc_id_get(),
                    .req = 0,
                    .seq = hdr.seq,
                    .size = sizeof(out_param),
                };
#if RPC_DEBUG_FLAG
                dbg_always("generate response: dest %d, src %d, req %d, seq %d, size %d", 
                    resp_hdr.dest, resp_hdr.src, resp_hdr.req, resp_hdr.seq, resp_hdr.size);
#endif
                size_t total_size = sizeof(rpc_header_t) + sizeof(rpc_out_param_t);
                char *resp_buf = (char*)mp_calloc(1, total_size);   // TODO: mp
                memcpy(resp_buf, &resp_hdr, sizeof(rpc_header_t));
                memcpy(resp_buf + sizeof(rpc_header_t), &out_param, sizeof(rpc_out_param_t));
                // 发送hdr + out_param，一次性发送
                if(ERR_NO_ERROR != uss_send(g_rpc_uss_mgnt, resp_buf, total_size))
                {
                    dbg_error("uss_send() rpc sync out fail");
                    mp_free(resp_buf, total_size);
                    continue;
                }
                mp_free(resp_buf, total_size);
#if RPC_DEBUG_FLAG
                dbg_always("send rpc sync ret and out param ok");
#endif
            }
        }
        else        // 处理响应
        {
            // 找到对应的sync sem，post一下唤醒
#if RPC_DEBUG_FLAG
            dbg_always("handle response");
#endif
            _rpc_call_sync_ctx_pool_set(p_hdr, (rpc_out_param_t*)p_ctx);
        }
    }
}

static void _rpc_other_proc_msg_handle()
{
    char tmp_buf[RPC_READ_ONCE_SIZE] = {};
    while(1)
    {
        memset(tmp_buf, 0, RPC_READ_ONCE_SIZE);
        ssize_t read_size = uss_recv(g_rpc_uss_mgnt, tmp_buf, RPC_READ_ONCE_SIZE);
        if(0 == read_size)
            break;
        else if(read_size < 0)
        {
            if(EINTR == errno)
                continue;
            else if(!(errno == EAGAIN || errno == EWOULDBLOCK))        // 非数据读取完毕
            {
                dbg_error("uss recv for select msg fail: %s", strerror(errno));
                break;
            }
        }

        size_t remain = read_size;
        size_t offset = 0;
        while(remain)
        {
            size_t write_size = fifo_write(g_non_rpc_fifo, tmp_buf + offset, remain);
            // 每次写入fifo，进行消息解析
            _non_rpc_fifo_handle();
            offset += write_size;
            remain -= write_size;
        }
        // 本次读取小于最大值，说明已经读取完毕
        if(read_size < RPC_READ_ONCE_SIZE)
            break;
    }
}

static void* _rpc_select_routine(void *args)
{
    ATOM_STORE(&g_rpc_select_thd_running, 1, MORDER_RELAXED);

    fd_set readfds;
    struct timeval tv = {};
    int fd = uss_mgnter_fd_get(g_rpc_uss_mgnt);

    while(ATOM_LOAD(&g_rpc_select_thd_running, MORDER_RELAXED))
    {
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(uss_mgnter_fd_get(g_rpc_uss_mgnt)+1, &readfds, NULL, NULL, &tv);      // 阻塞等待就绪

        if(ret > 0)
        {
            if(FD_ISSET(fd, &readfds))
                // 全部读取到fifo，然后待处理
                _rpc_other_proc_msg_handle();
        }
        else if(ret == 0)       // timeout
        {}
        else
        {
            if(errno == EINTR)
                continue;
            dbg_error("select() fail: %s", strerror(errno));
            break;
        }
    }
}

void rpc_module_init()
{
    if(proc_id_get() == PID_RPC)
        _rpc_module_init_for_rpc();
    else
        _rpc_module_init_for_others();
}

static void _rpc_module_exit_for_rpc(void)
{
    proc_id_e pid;

    PROC_ID_ITER_ALL(pid)
    {
        if(pid == PID_RPC)
            continue;
        uss_mgnter_destory(g_rpc_uss_mgnt_table[pid].accepter);
        uss_mgnter_destory(g_rpc_uss_mgnt_table[pid].listener);
    }
}

static void _rpc_module_exit_for_others(void)
{
    // 结束select线程
    ATOM_STORE(&g_rpc_select_thd_running, 0, MORDER_RELAXED);
    pthread_join(g_rpc_select_tid, NULL);

    // 释放uss
    uss_mgnter_destory(g_rpc_uss_mgnt);

    // 销毁fifo
    fifo_destroy(&g_non_rpc_fifo);

    // 清理rpc服务注册表
    ev_with_wrlock(&g_rpc_server_func_entry_table_rwlock)
    {
        rpc_server_func_entry_t *entry = NULL;
        while(entry = rpc_server_func_entry_hash_first(&g_rpc_server_func_entry_table))
        {
            rpc_server_func_entry_hash_del(&g_rpc_server_func_entry_table, entry);
            mp_free(entry, sizeof(rpc_server_func_entry_t));
        }
    }
}

void rpc_module_exit()
{
    if(proc_id_get() == PID_RPC)
        _rpc_module_exit_for_rpc();
    else
        _rpc_module_exit_for_others();
}

static void* _rpc_epoll_thd_routine(void *args)
{
    // epoll init
    g_rpc_epoll_fd = epoll_create1(O_CLOEXEC);
    pfm_ensure_ret(g_rpc_epoll_fd >= 0, NULL);
    // add epoll
    for(proc_id_e pid = PID_CORE; pid < PID_CNT; ++ pid)
    {
        if(pid == PID_RPC)
            continue;

        int fd = uss_mgnter_fd_get(g_rpc_uss_mgnt_table[pid].listener);
        struct epoll_event event = {
            .data.fd = fd,
            .events = EPOLLIN | EPOLLET,
        };
        pfm_ensure_ret(0 == epoll_ctl(g_rpc_epoll_fd, EPOLL_CTL_ADD, fd, &event), NULL);
    }

    prctl(PR_SET_NAME, "rpc_epoll");

    struct epoll_event events[RPC_EPOLL_LISTEN_CNT];
    ATOM_STORE(&g_rpc_epoll_thd_running, 1, MORDER_RELAXED);
    while(ATOM_LOAD(&g_rpc_epoll_thd_running, MORDER_RELAXED))
    {
        int nfds = epoll_wait(g_rpc_epoll_fd, events, RPC_EPOLL_LISTEN_CNT, RPC_EPOLL_WAIT);
        if(nfds < 0)
            if(errno == EINTR)      continue;
            else
            {
                dbg_error("epoll wait error: %s", strerror(errno));
                break;
            }

        for(int i = 0; i < nfds; ++ i)
        {
            int fd = events[i].data.fd;
            proc_id_e conn_pid;
            if(_rpc_check_new_conn(fd, &conn_pid))         // 处理新的连接
                _rpc_handle_new_conn(conn_pid);
            else            // 收到消息
            {
#if RPC_DEBUG_FLAG
                dbg_always("rpc broker receive msg, handle");
#endif
                _rpc_handle_msg(fd);
            }
        }
    }

    // exit
    close(g_rpc_epoll_fd);      // 自动del
}

void rpc_proc_entry()
{
    // 开辟共享内存用于检查运行标志
    rpc_process_running = shm_mgnt_open(RPC_PROCESS_RUNNING_SHM_NAME, sizeof(int), 1);
    pfm_ensure_ret_void(rpc_process_running);
    int run = 1;
    shm_mgnt_write(rpc_process_running, 0, &run, sizeof(run));

    // 创建环形缓冲用于分析uss rpc消息
    g_rpc_fifo = fifo_create(RPC_FIFO_SIZE);

    // epoll监听线程启动
    pthread_create(&g_rpc_epoll_tid, NULL, _rpc_epoll_thd_routine, NULL);

    while(1)
    {
        // 检查shm运行标志
        shm_mgnt_read(rpc_process_running, 0, &run, sizeof(run));
        if(!run)
            break;

        usleep(100 * 1000);
    }

    shm_mgnt_close(rpc_process_running);

    fifo_destroy(&g_rpc_fifo);

    ATOM_STORE(&g_rpc_epoll_thd_running, 0, MORDER_RELAXED);
    pthread_join(g_rpc_epoll_tid, NULL);

    // unlink all socket file
    for(proc_id_e pid = PID_CORE; pid < PID_CNT; ++ pid)
        unlink(g_rpc_uss_path_table[pid]);

    dbg_always("@@@ rpc process exit...");
}

// TODO: 
error_code_e rpc_call_func_async(proc_id_e dest, const char* func, rpc_in_param_t *in_param)
{
    pfm_ensure_ret(PROC_ID_IS_VALID(dest) && func && strlen(func) < RPC_FUNC_NAME_SIZE, ERR_BAD_PARAM);

    // buffer初始化
    rpc_header_t buffer = {
        .src = proc_id_get(),
        .dest = dest,
        .sync = 0,              // 异步调用
    };
    strncpy(buffer.func, func, sizeof(buffer.func) - 1);
    size_t ctx_len = 0;
    // TODO: + ctx

    buffer.size = ctx_len;
    error_code_e ret = uss_send(g_rpc_uss_mgnt, &buffer, sizeof(rpc_header_t) + ctx_len);
    pfm_ensure_ret(ret == ERR_NO_ERROR, ret);

    dbg("rpc call func async done, src %d, dest %d, func %s", proc_id_get(), dest, func);

    return ERR_NO_ERROR;
}

static size_t _rpc_call_sync_ctx_pool_add(rpc_header_t *hdr)
{
    ev_with_mutex(&g_rpc_call_sync_ctx_pool_mutex)
    {
        // 遍历数组，找到valid = 0的槽位覆盖数据
        for(int i = 0; i < RPC_SYNC_POOL_SIZE; ++ i)
        {
            if(g_rpc_call_sync_ctx_pool[i].valid == 0)
            {
                g_rpc_call_sync_ctx_pool[i].src = hdr->src;
                g_rpc_call_sync_ctx_pool[i].dest = hdr->dest;
                g_rpc_call_sync_ctx_pool[i].seq = hdr->seq;
                g_rpc_call_sync_ctx_pool[i].valid = 1;
                return i;
            }
        }
    }

    return RPC_SYNC_POOL_SIZE;
}

static error_code_e _rpc_call_sync_ctx_pool_set(rpc_header_t *hdr, rpc_out_param_t *out_param)
{
    ev_sem_t *target_sem = NULL;
    error_code_e ret = ERR_RPC_SYNC_CTX_ERROR;

    ev_with_mutex(&g_rpc_call_sync_ctx_pool_mutex)
    {
        for(int i = 0; i < RPC_SYNC_POOL_SIZE; ++ i)
        {
            if(!g_rpc_call_sync_ctx_pool[i].valid)  continue;

            rpc_call_sync_ctx_t *ctx = &g_rpc_call_sync_ctx_pool[i];
            if(hdr->seq == ctx->seq && hdr->src == ctx->dest && hdr->dest == ctx->src)
            {
                memcpy(&ctx->out_param, out_param, sizeof(rpc_out_param_t));
                target_sem = &ctx->sem;
                ret = ERR_NO_ERROR;
                break;
            }
        }
    }

    if(target_sem)
        ev_sem_post(target_sem);

    return ret;
}

error_code_e rpc_call_func_sync(proc_id_e dest, const char* func, rpc_in_param_t *in_param, rpc_out_param_t *out_param, uint64_t wait_time)
{
    pfm_ensure_ret(PROC_ID_IS_VALID(dest) && func && out_param && strlen(func) < RPC_FUNC_NAME_SIZE, ERR_BAD_PARAM);

    // rpc header
    uint32_t seq = PRC_SYNC_SEQ_NEW;
    rpc_header_t hdr = {
        .src = proc_id_get(),
        .dest = dest,
        .sync = 1,
        .req = 1,
        .seq = seq,
    };
    strncpy(hdr.func, func, RPC_FUNC_NAME_SIZE-1);
    // TODO: rpc buffer

    // TODO: call and sem wait
    // note in pool
    size_t index;
    pfm_ensure_ret(RPC_SYNC_POOL_SIZE != (index = _rpc_call_sync_ctx_pool_add(&hdr)), ERR_RPC_SYNC_POOL_FULL);
    // send pkt to rpc broker
    error_code_e ret;
    pfm_ensure_ret(ERR_NO_ERROR == (ret = uss_send(g_rpc_uss_mgnt, &hdr, sizeof(rpc_header_t))), ret);

#if RPC_DEBUG_FLAG
    dbg_always("@@@ rpc send ok, wait for waking");
#endif

    // wait for sem
    if(RPC_SYNC_CALL_WAIT_FOREVER != wait_time)
    {
        ev_sem_ret_e sem_ret = ev_sem_wait_ms(&g_rpc_call_sync_ctx_pool[index].sem, wait_time);
        if(EV_SEM_TIMEOUT == sem_ret)
        {
            dbg_error("rpc call func sync timeout, src %d, dest %d, func %s, seq %u", proc_id_get(), dest, func, seq);
            ev_with_mutex(&g_rpc_call_sync_ctx_pool_mutex)      // 清理上下文
                g_rpc_call_sync_ctx_pool[index].valid = 0;
            return ERR_RPC_SYNC_TIMEOUT;
        }
    }
    else
        ev_sem_wait(&g_rpc_call_sync_ctx_pool[index].sem);
#if RPC_DEBUG_FLAG
    dbg_always("@@@ rpc wake up");
#endif
    // 获取返回值，将valid置为0，释放pool单元
    ev_with_mutex(&g_rpc_call_sync_ctx_pool_mutex)
    {
        // 浅拷贝给调用方，tlvs内存需要调用方释放
        memcpy(out_param, &g_rpc_call_sync_ctx_pool[index].out_param, sizeof(rpc_out_param_t));
        g_rpc_call_sync_ctx_pool[index].valid = 0;        // clear valid
    }
#if RPC_DEBUG_FLAG
    dbg_always("@@@ rpc sync call done, ret %d\n\n", out_param->ret);
#endif
    return out_param->ret;
}