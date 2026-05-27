# list

链表的代码位于[type_list.h](../../lib/include/type/type_list.h)和[type_list.c](../../lib/type/type_list.c)

本项目学习并实现了一个侵入式链表模板。

## “侵入式”链表的含义

所谓侵入式，是指链表相关的结构被侵入到业务数据中

我们熟悉的通用链表节点定义一般如下

```C
struct list_node{
    void *data;     // 业务数据
    struct list_node *next;
};
```

这种链表将业务数据和链表的结构指针分开，在追求高性能，资源受限，对延迟敏感的场景下存在诸多缺陷

- 内存开销：每个数据都得malloc一个链表节点来存储
- 缓存命中率低：节点内存和数据内存一般是分离的，拿到链表节点后，需要跳转再去读取实际数据，容易CPU Cache Miss

侵入式链表的节点定义一般为

```C
typedef struct list_item_s{
    struct list_item_s *next;
}list_item_t;
```

而业务数据定义需要手动包含`list_item_t`成员。例如

```C
typedef struct{
    unsigned int data;
    list_item_t item;       // 链表侵入
}data_t;
```

这样做的好处就是无需额外分配内存，并且业务数据和指针连续，少了一次跳转

此外，

- 一个对象可以同时属于多个链表，只需要在业务数据结构中多定义几个链表`item`成员。且后续可以扩展到对象同时存在于链表、队列、哈希表中...

## 宏代码生成

项目使用了宏代码生成的技巧，目的是避免为每个业务模块重复编写链表操作代码

业务模块可以使用`pre_declare_list`和`declare_list`宏，其中以下传入参数，即可生成专用的一套侵入式链表变量、函数实现。

- `sprefix`：struct prefix
- `fprefix`：function prefix
- `type`：业务数据类型
- `field`：业务数据中侵入链表成员名

详见[type_list.h](../../lib/include/type/type_list.h)

## 测试

测试代码位于[test_type_list.c](../../entry/test/test_type_list.c)

其中测试了单个接口，然后简要实现了非侵入式链表，针对add和pop操作做了性能对比。可见在空间和时间性能上差距不小

```
test type list result: PASS
========
test type list cost: PASS
add 100000 item into list cost: 5741 us, 0.057 i/us
pop 100000 item from list cost: 5882 us, 0.059 i/us
alloc mem: 1600024 Bytes
free mem: 1600024 Bytes
========
test normal list cost: PASS
add 100000 item into normal list cost: 26465 us, 0.265 i/us
pop 100000 item from normal list cost: 16417 us, 0.164 i/us
alloc mem: 2000024 Bytes
free mem: 2000024 Bytes
========
```

运行环境为树莓派3B，平均60-ns的增删性能，问了大模型算是不错的成绩