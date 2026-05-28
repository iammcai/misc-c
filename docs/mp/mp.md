# mem pool

## 设计

首先是一个全局的内存类型属性`mem_type_attr_t`，使用一个全局哈希表`g_mem_type_attr_hash_head`来存储所有类型。

```C
// 内存类型属性
typedef struct{
    const char *name;                   // 类型名
    unsigned int flag;                  // 标志位
    unsigned int node_size;             // 该类型的内存节点大小
    unsigned int node_max_num;          // 该类型的最大节点数量
    mem_type_attr_hash_item_t item;     // hash item
}mem_type_attr_t;
```

定义构造函数`g_mem_type_attr_hash_init`来确保进入`main`函数前初始化

用户需要声明一个业务对应的内存类型，后续使用这个类型来进行内存申请和释放

接着定义固定大小的内存节点类型`fixed_mem_node_t`，其中使用柔性数组来存给用户的真正内存

```C
// 固定大小内存节点定义
typedef struct{
    mem_type_attr_t *attr;                  // 所属内存类型
    fixed_mem_node_free_list_item_t item;   // free list item
    unsigned int size;                      // 用户内存大小
    char attr_aligned(8) data[];            // 柔性数组，真正给用户使用的内存区
} attr_aligned(8) fixed_mem_node_t;
```

使用一个单链表free_list来存储所有内存节点，用户申请时从链表头获取，释放时也放回链表头

所有的空闲单链表头加入一个全局哈希表

```C
// 固定大小空闲内存链表头定义，哈希表存储起来
typedef struct{
    fixed_free_list_head_t  *head;  // 空闲链表头
    mem_type_attr_t *attr;          // 所属内存类型
    fixed_free_list_head_hash_item_t item;  // item
}fixed_free_list_t;
```