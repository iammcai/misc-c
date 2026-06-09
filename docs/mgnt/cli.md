# CLI

本项目欲实现一个简单好用的CLI框架，基于此实现对项目软件的管理

## 设计思路

### CLI命令的存储

本项目中支持的CLI命令应该不多，估计在几十条的量级，可以考虑数组存储，使用时线性扫描，实现简单。

但刚需功能支持模糊匹配，使用数组的话效果就不太好了。如此来看的话，还是选择实现一个字典树来进行存储。

前缀树定义：

```C
// cli前缀树节点定义
typedef struct cli_trie_item_s{
    const char *name;                   // cmd中的一个单词，需要动态申请内存
    struct cli_trie_item_s **next;      // next array
    unsigned char size;                 // next array size
    char is_end;                        // 是否为cli的结束节点
    cli_hook_func hook;                 // cli回调，is_end == 1 有效
    const char *help;                   // help信息，同上
}cli_trie_item_t;

// cli前缀树定义
typedef struct{
    cli_trie_item_t root;   // 根节点，作为哨兵使用
}cli_trie_root_t;
```

设计上各个模块初始化向CLI注册命令，后续修改少，因此使用动态分配所需内存，对运行时影响不大

前缀树需要支持插入、查找、展示，实现还是比较简单的，不再赘述。

### CLI流程

CLI需要持续监听用户输入，因此在`cli_init`中创建一个线程用于监听用户输入

输入进来后，处理为标准的cmd格式，在前缀树中查找对应hook执行

CLI是串行执行的，需要等待一条CLI执行完毕，再开始接受用户输入，继续执行，因而采用单个线程就好

目前只有用户输入作为来源，大概率不会有其它输入来源，因此无需消息队列中介

## 代办项

- 目前不支持参数输入，后续有需求补充
- 目前不支持模糊匹配，CLI数量多了有需求再补充
