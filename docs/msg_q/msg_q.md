# msg queue

嵌入式开发中，模块解耦以及模块之间异步通信时刚需。常见的消息队列库功能强大，但依赖较多、移植麻烦，因此本项目实现一个轻量级的消息队列，后续在项目中使用，例如用于开发日志模块。

## 设计思路

从消息队列使用的场合出发，本文设计的消息队列提供以下特性：

- 支持MPSC，允许多个线程推送消息，一个线程接收消息
- 接收方支持阻塞/非阻塞读
- 消息队列自行管理一块定长的环形缓冲区用于消息存储

消息队列的定义如下，其中：

- 使用自旋锁来支持并发写
- 使用信号量在阻塞读的场景下，通知接收线程可读
- 柔性数组`ring_buffer`为环形缓冲区，在创建消息队列时再根据需求大小申请内存

```C
// 消息队列结构定义
typedef struct{
    const char *name;           // 消息队列名
    size_t head;                // 头节点Index
    size_t tail;                // 尾节点Index+1
    size_t capacity;            // 最大容量
    size_t elem_size;           // 消息大小
    size_t size;                // 当前长度
    ev_spinlock_t spinlock_in;  // 自旋锁，用于互斥写入消息
    ev_sem_t sem_out;           // 信号量，用于通知读取消息
    char ring_buffer[];         // 柔性数组，环形缓冲区
}msg_q_t;
```

消息队列对外提供的接口，最基本的有：创建/销毁消息队列，以及推送消息、读取消息

### 消息队列创建/销毁

本文提供静态和动态方式创建消息队列

静态创建，通过使用`constructor`的编译属性，在构造函数中完成，这个大家在之前的文章中也很熟悉了

```C

#define attr_ctor(x)        __attribute__((constructor(x)))                 // 指定构造函数，在main函数执行前会自动运行

/**
 * 外部使用，声明定义一个消息队列
 */
#define declare_msg_q(_name, _capacity, _elem_size) \
static msg_q_t* msg_q_ ## _name = NULL; \
static attr_force_inline void _msg_q_ ## _name ## _init() attr_ctor(CTOR_PRIO_MID); \
static attr_force_inline void _msg_q_ ## _name ## _init()   \
{   \
    msg_q_ ## _name = mp_calloc(1, sizeof(msg_q_t) + _capacity * _elem_size);   \
    assert(msg_q_ ## _name);    \
    msg_q_ ## _name->name = #_name; \
    msg_q_ ## _name->capacity = _capacity;  \
    msg_q_ ## _name->elem_size = _elem_size;    \
    ev_spinlock_init(&msg_q_ ## _name->spinlock_in);    \
    ev_sem_init(&msg_q_ ## _name->sem_out);  \
}   \
/* declare_msg_q end */
```

动态创建可见`msg_q_t* msg_q_create(const char *name, size_t capacity, size_t elem_size)`接口，接口流程和以上一致。注意其中根据入参中的消息最大数量和消息的大小，来申请柔性数组空间

销毁消息队列，主要是释放创建时申请的内存，具体可见`void msg_q_destroy(msg_q_t **p_mq)`。本文约束调用销毁接口时，调用方保证消息读取完毕

### 消息推送

消息推送的接口如下。

为支持多个线程同时推送消息，因此首先需要获取自旋锁，而后若环形缓冲区有空，则拷贝消息到缓冲区中，否则直接释放锁，返回error。

至于环形缓冲区的用法，一般是通过头尾两个指针递增、回绕来进行管理，也不必过多赘述。

此外若推送消息前缓冲区为空，则需要写信号量，通知正在阻塞的线程读取消息。

此处使用的`ev_spinlock`、`ev_sem`，均在此前的文章中有介绍

```C
// 消息队列错误码枚举
typedef enum{
    msg_q_ret_ok = 0,
    msg_q_ret_full,
    msg_q_ret_non_msg,
}msg_q_ret_type_e;

msg_q_ret_type_e _msg_q_push(msg_q_t *msg_q, void *ctx, size_t len)
{
    assert(msg_q && ctx && len <= msg_q->elem_size);
    size_t size = 0;

    ev_with_spinlock(&msg_q->spinlock_in)
    {
        if(ATOM_LOAD(&msg_q->size, MORDER_ACQUIRE) < msg_q->capacity)
        {
            // 入队
            memcpy(msg_q->ring_buffer + msg_q->tail*msg_q->elem_size, ctx, len);
            ++ msg_q->tail;
            if(msg_q->tail >= msg_q->capacity)  // 回绕
                msg_q->tail = 0;
            size = ATOM_FETCH_ADD(&msg_q->size, 1, MORDER_RELEASE);
        }
        else
            return msg_q_ret_full;
    }

    if(!size)   // 入队前为空，通知可以pop
        ev_sem_post(&msg_q->sem_out);

    return msg_q_ret_ok;
}
```

### 消息读取

读取消息支持阻塞和非阻塞两种方式，前者通过信号量来进行等待。

由于只支持单读者，因此无需加锁。接口如下：

```C
// 等待类型枚举
typedef enum{
    msg_q_no_wait,
    msg_q_wait_forever
}msg_q_wait_type_e;

msg_q_ret_type_e _msg_q_pop(msg_q_t *msg_q, size_t len, msg_q_wait_type_e wait_type, void *ctx)
{
    assert(msg_q && len <= msg_q->elem_size && ctx);

    while(ATOM_LOAD(&msg_q->size, MORDER_ACQUIRE) == 0)
    {
        if(msg_q_no_wait == wait_type)
            return msg_q_ret_non_msg;
        else
            ev_sem_wait(&msg_q->sem_out);       // 等待
    }

    // 获取数据
    memcpy(ctx, msg_q->ring_buffer + msg_q->head*msg_q->elem_size, len);
    ++ msg_q->head;
    if(msg_q->head >= msg_q->capacity)
        msg_q->head = 0;

    ATOM_FETCH_SUB(&msg_q->size, 1, MORDER_RELEASE);    // -1

    return msg_q_ret_ok;
}
```

## 总结

当前的设计中，只提供了MPSC场景，目标是覆盖后续的日志模块需求（在旧版的zcap，零拷贝抓包模块也曾使用过）

当前设计在多消费者支持、超时等待、数据零拷贝等方面还有可优化空间，就留待后续有需求，再进行迭代。