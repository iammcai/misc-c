# ATOM SPSC QUEUE

# SPSC ATOM QUEUE

## Memory Order

C11定义六种内存序。内存序是为了解决两个问题

- 重排序：编译器和CPU为了优化性能，可能会改变指令的执行顺序
- 可见性：一个线程对变量的修改，何时对其他线程可见

## Single Producer Single Consumer queue

本项目实现了一个单生产者-单消费者场景下的侵入式原子队列

此处分析一下入队和出队的接口，见注释

```C

// 队列节点定义
typedef struct{
    ATOMIC_UINTPTR_T next;
}spsc_atom_queue_item_t;

// 队列管理结构定义
typedef struct{
    spsc_atom_queue_item_t first_item;      // 哨兵节点
    ATOMIC_UINTPTR_T last_next;             // 指向队尾的next
    unsigned int count;                     // 队列长度
}spsc_atom_queue_head_t;


// 入队，由于只有一个生产者，所以不用处理竞争
void type_spsc_atom_queue_push(spsc_atom_queue_head_t *head, spsc_atom_queue_item_t *item)
{
    uintptr_t prev_next = 0;

    if(0 == ATOM_FETCH_ADD(&head->count, 1, MORDER_RELAXED))    // 队列长度+1，如果原来是0，那么初始化
        type_spsc_atom_queue_init(head);    // 这里修改了last_next，是很有必要的，因为pop接口里弹出后没有操作last_next，弹出后空队列，缺少修改，这里刚好补上了

    ATOM_STORE(&item->next, ATOM_PTR_NULL, MORDER_RELAXED);     // 设置item->next为NULL，即将插入队列尾部
    prev_next = ATOM_XCHG(&head->last_next, ATOM_PTR2UNIT(&item->next), MORDER_ACQ_REL);    // 将last_next修改为item->next，并且记录原来的last_next
    ATOM_STORE((ATOMIC_UINTPTR_T*)ATOM_UINT2PTR(prev_next), ATOM_PTR2UNIT(item), MORDER_RELEASE);   // 设置原来的last_next为item，相当于将原来的队尾的next指向了加入的item
}

// 出队，只有一个消费者，不用处理竞争
spsc_atom_queue_item_t* type_spsc_atom_queue_pop(spsc_atom_queue_head_t *head)
{
    uintptr_t first = 0;
    uintptr_t expect = 0;
    uintptr_t next = 0;
    spsc_atom_queue_item_t *it = NULL;
    unsigned int n = 0;

    first = ATOM_LOAD(&head->first_item.next, MORDER_ACQUIRE);  // 获取队列头部
    if(!first)              // 空队列，则失败
        return NULL;

    expect = first;         // expect就是指向要出队的item
    it = ATOM_UINT2PTR(first);
    n = ATOM_SUB_FETCH(&head->count, 1, MORDER_RELAXED);    // 将count-1，获取此时的count
    if(n)   // 如果此时count>0，说明还有元素，或者是生产者+1
    {
        do  // 可能生产者++count，但还没真正挂上去。这里dowhile等待挂上，获取next
        {
            next = ATOM_LOAD(&it->next, MORDER_ACQUIRE);
        }while(!next);
    }

    // CAS，当头节点的next确实还是expect时，修改为下一个next。
    // 对于SC，可以直接store，已修改验证ok
    while(!ATOM_CMP_XCHG_WEAK(&head->first_item.next, &expect, next, MORDER_ACQ_REL, MORDER_RELAXED))
    {
        if(expect != first) // 处理weak可能的伪失败
            break;
    }

    return it;
}
```