# event loop

一开始是想把项目中所有的epoll+fd都统一到一个模块进行管理，一番了解下得知这是一种主流的设计模式，名为reactor

reactor模式的核心是“等待-分发”：

- 等待：在一个IO多路复用器（比如epoll）上等待事件就绪
- 分发：根据就绪的事件类型（可读或可写），调用预先注册的回调函数，通知业务进行处理

## 架构设计

根据reactor模式的思路，大体可以明确我们需要在一个线程中循环等待事件就绪，进行分发。本项目选择使用epoll，可以管理大量的连接，全部使用边缘触发+非阻塞IO，减少系统调用次数

定义相关数据结构如下。其中重点关注`el_t`：

```C
// file mask 枚举定义
typedef enum{
    EL_FILE_EVENT_NONE = 0,             // 未注册，用于安全覆盖
    EL_FILE_EVENT_READABLE = 1 << 0,    // 注册了可读事件
    EL_FILE_EVENT_WRITEABLE = 1 << 1,   // 注册了可写事件
}el_file_event_mask_e;

// file快照结构定义
typedef struct{
    int fd;                 // 就绪fd
    unsigned char mask;     // 事件类型mask
}el_file_event_fired_t;

// file事件结构定义
typedef struct{
    unsigned char mask;         // 掩码，表示监听读写事件，见el_file_event_mask_e
    el_file_event_cb read_cb;   // 可读事件回调
    el_file_event_cb write_cb;  // 可写事件回调
    void *args;                 // 回调参数
}el_file_event_t;

// 事件循环管理结构定义
typedef struct el_s{
    int epoll_fd;               // epoll fd

    el_file_event_t *file_events;           // 存储file事件的数组，以fd为下标。为了性能，不使用哈希表
    el_file_event_fired_t *file_fireds;     // file_events运行时快照，防止多个fd互相操作破坏结构

    el_fe_reg_list_head_t file_events_reg_list;     // 用于运行时修改事件的链表
    ev_spinlock_t file_events_reg_spinlock;         // 保护file_events_reg_list
    int file_events_reg_event_fd;                   // eventfd，用于唤醒处理注册

    pthread_t ev_loop_th;       // 进行loop的线程id
    #define EV_LOOP_FILE_EVENT_COUNT_MAX    (512)               // 支持的file事件最大数量
    struct epoll_event events[EV_LOOP_FILE_EVENT_COUNT_MAX];    // 事件缓冲区
}el_t;
```

- `epoll_fd`：将所有的fd注册到这个epoll进行管理
- `file_events`：数组，存放所有注册进来的fd，数组下标即为fd。
    - 内部记录注册掩码、事件触发回调函数及参数
    - 考虑到本项目使用的fd少，且比较紧凑，所以不使用hashtable
- `file_fireds`：数组，存放每次事件处理时的快照
    - 避免例如一轮中处理fdA和fdB，而fdA处理中注销了fdB，直接操作`file_events`可能出问题
- `file_events_reg_list`：一个侵入式链表，用于运行时动态注册/注销fd到ev_loop中
- `file_events_reg_spinlock`：用来保护上述链表，以支持多个业务同时提交注册/注销fd到ev_loop中
- `file_events_reg_event_fd`：初始册到ev_loop的eventfd，当注册/注销fd任务产生时，写这个fd唤醒等待，进行处理
- `ev_loop_th`：ev_loop运行的子线程ID
- `events`：ev_loop主循环使用的就绪事件缓冲区，放在这里，避免在热点路径上重复使用栈内存

整体运行逻辑如下图

![ev_loop](../images/ev_loop.png)

### 事件循环核心

执行“等待-分发”的循环如下

```C
static void* _el_main(void *args)
{
    while(1)
    {
        // 阻塞等待事件
        int ready = _el_epoll_wait();
        // 处理被信号打断的场合
        if(ready < 0)
            if(errno == EINTR)
                continue;
            else
            {
                dbg_error("_el_epoll_wait fatal error: %s", strerror(errno));
                break;
            }

        // 进行事件分发，以fired数组为准
        int i = 0;
        for(; i < ready; ++ i)
        {
            int fd = g_event_loop.file_fireds[i].fd;
            // 检查类型，看是否需要这里消耗掉可读
            el_file_event_t *fe = g_event_loop.file_events + fd;
            if(fe->type == EL_FILE_EVENT_TYPE_EVENTFD)
                eventfd_read_all(fd);
            else if(fe->type == EL_FILE_EVENT_TYPE_TIMERFD)
                timerfd_read_all(fd);

            if(fd == g_event_loop.file_events_reg_event_fd)     // 注册新的事件
                _el_reg_fe_handle();
            else if(fd >= 0 && fd < EV_LOOP_FILE_EVENT_COUNT_MAX)   // 分发事件
            {
                //dbg_always("distribute fd %d", fd);
                unsigned char fired_mask = g_event_loop.file_fireds[i].mask;
                if((fired_mask & fe->mask & EL_FILE_EVENT_READABLE) && fe->read_cb)
                    fe->read_cb(fe->args);
                if((fired_mask & fe->mask & EL_FILE_EVENT_WRITEABLE) && fe->write_cb)
                    fe->write_cb(fe->args);
            }
        }
    }
    return NULL;
}
```

- 在`_el_epoll_wait`中阻塞等待事件就绪，并处理fired数组
- 分发所有就绪事件，包括注册/注销请求和其他用户事件

可以看到这里是一个单线程reactor，分发动作是串行执行回调，因此约束用户注册的回调函数不能有耗时操作。本文的做法是在回调函数中通过信号量唤醒线程 or 投递任务到线程池

### 事件注册/注销

由于项目中，其他组件大多支持运行时动态启动/停止，因此ev_loop支持动态注册/注销事件。

目前将事件类型划分为三类：普通事件（例如socket）、eventfd以及timerfd，后两者主要是将一次性读完缓冲区的操作放在ev_loop中，防止用户注册的回调函数遗漏

```C
// file事件类型枚举
typedef enum{
    EL_FILE_EVENT_TYPE_NORMAL = 0,      // 普通
    EL_FILE_EVENT_TYPE_EVENTFD,         // eventfd，内核计数器
    EL_FILE_EVENT_TYPE_TIMERFD,         // timerfd，内核高精度定时器

    EL_FILE_EVENT_TYPE_CNT,             // 用于计数
}el_file_event_type_e;
```

注册/注销事件的接口如下，其实就是事件item加入链表，唤醒ev_loop处理

```C
void _el_reg_fe(int fd, el_file_event_type_e type, unsigned char mask, el_file_event_cb cb_func, void *args)
{
    pfm_ensure_ret_void(fd >= 0);

    // 申请内存，注册事件较少，直接动态申请
    el_fe_reg_item_t *it = mp_calloc(1, sizeof(el_fe_reg_item_t));
    it->fd = fd;
    it->el_file_event.type = type;
    it->el_file_event.mask = mask;
    it->el_file_event.args = args;
    if(mask & EL_FILE_EVENT_READABLE)
        it->el_file_event.read_cb = cb_func;
    if(mask & EL_FILE_EVENT_WRITEABLE)
        it->el_file_event.write_cb = cb_func;

    // 加到链表中
    ev_with_spinlock(&g_event_loop.file_events_reg_spinlock)
        el_fe_reg_list_add_tail(&g_event_loop.file_events_reg_list, it);

    dbg_major("call register fd %d into event loop", fd);

    // 写eventfd唤醒epoll进行注册
    uint64_t val = 1;
    pfm_ensure_ret_void(sizeof(val) == write(g_event_loop.file_events_reg_event_fd, &val, sizeof(val)));
}
```

ev_loop中处理的接口如下，将链表中的节点取出，修改`file_events`，且修改epoll注册的事件

此处根据记录的掩码来选择EPOLL_CTL_ADD或者MOD，支持增量注册

```C
static void _el_reg_fe_handle()
{
    // 遍历注册队列，取出来
    el_fe_reg_item_t *it = NULL;
    ev_with_spinlock(&g_event_loop.file_events_reg_spinlock)
        it = el_fe_reg_list_pop(&g_event_loop.file_events_reg_list);
    while(it)
    {
        int index = it->fd;     // 以fd作为数组索引
        if(!(index >=0 && index < EV_LOOP_FILE_EVENT_COUNT_MAX))    // 超出索引处理
        {
            dbg_error("fd %d out of range", index);
            mp_free(it, sizeof(el_fe_reg_item_t));
            continue;
        }
        // file_events数组中对应槽位
        el_file_event_t *fe = g_event_loop.file_events + index;
        if(EL_FILE_EVENT_NONE != it->el_file_event.mask)
        {
            // 获取原来的mask，可能原来注册了read，要补充注册write，直接EPOLL_CTL_ADD会错误呢，必须MOD
            int epoll_op = fe->mask == EL_FILE_EVENT_NONE ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
            unsigned char mask = it->el_file_event.mask | fe->mask;

            struct epoll_event ev = {.data.fd = it->fd, .events = EPOLLET};
            if(mask & EL_FILE_EVENT_READABLE)
                ev.events |= EPOLLIN;
            if(mask & EL_FILE_EVENT_WRITEABLE)
                ev.events |= EPOLLOUT;
            epoll_ctl(g_event_loop.epoll_fd, epoll_op, it->fd, &ev);

            // 写入到全局file_event array，需要增量修改
            if(it->el_file_event.mask & EL_FILE_EVENT_READABLE)
                fe->read_cb = it->el_file_event.read_cb;
            if(it->el_file_event.mask & EL_FILE_EVENT_WRITEABLE)
                fe->write_cb = it->el_file_event.write_cb;
            if(fe->args == NULL || it->el_file_event.args != NULL)
                fe->args = it->el_file_event.args;
            fe->mask = mask;

            dbg_always("register fd %d into loop done", it->fd);
        }
        else        // 注销
        {
            // 从epoll移除
            epoll_ctl(g_event_loop.epoll_fd, EPOLL_CTL_DEL, it->fd, NULL);
            fe->mask = EL_FILE_EVENT_NONE;      // 清除数组标记
            dbg_always("deregister fd %d in loop", it->fd);
        }
        // 释放内存
        mp_free(it, sizeof(el_fe_reg_item_t));

        // 获取下一个
        ev_with_spinlock(&g_event_loop.file_events_reg_spinlock)
            it = el_fe_reg_list_pop(&g_event_loop.file_events_reg_list);
    }
}
```

### 适配案例

实现ev_loop后，本项目把其他模块原先使用epoll的部分也修改了一下，包括zcap、ev_high_res_timer和ev_thd

这里以ev_timer.c中高精度定时器为例介绍。相关提交可见`commit-id:f487322b5cda0a72bd5bb6882ec58bdb9d95f04d`

创建hr_timer的接口中，增加了将timerfd注册到ev_loop中

```C
ev_high_res_timer_t* ev_high_res_timer_create(const char *name, uint64_t timeout, ev_timer_cb_func cb, void *args)
{
    ...

    // tfd注册到evloop
    event_loop_register_file_event_timerfd(hr_timer->tfd, EL_FILE_EVENT_READABLE, _ev_high_res_timer_el_cb, hr_timer);

    ...
}
```

其中，定时器到期的事件由ev_loop捕捉到，执行回调函数如下：仅执行了线程池初始化（若需要）以及任务投递，不直接进行用户注册回调避免耗时

```C
/**
 * @brief       hr_timer tfd readable event, cb
 * 
 * @param[in]   args    - hr_timer
 * 
 * @note        高精度定时器到期时，ev loop调用，将回调传给线程池执行
 */
static void _ev_high_res_timer_el_cb(void *args)
{
    ev_high_res_timer_t *hr_timer = (ev_high_res_timer_t*)args;

    // 线程池未启动的话，初始化启动一下。这是因为thp包含mp，这个函数由ev loop调用，那边没有初始化mp相关
    if(!_thp_is_run(&_thp_ev_high_res_timer))
    {
        thp_init(&_thp_ev_high_res_timer, "ev_high_res_timer", THREAD_POOL_TH_COUNT_MAX);
        thp_run(ev_high_res_timer);
    }

    thp_submit_task(ev_high_res_timer, hr_timer->cb, hr_timer->args);   // 投递到线程池
}
```

在ev_thread.c中适配时处理方式略有不同，ev_loop回调函数中是写信号量唤醒线程，但共同点是不做阻塞任务，提交在`commit-id:151db7faa9594b469e9fb1f40e25f036f7b42961`

