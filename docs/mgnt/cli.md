# CLI

本项目欲实现一个简单好用的CLI框架，基于此实现对项目软件的管理

## 设计思路

### CLI命令的存储

本项目中支持的CLI命令应该不多，估计在几十条的量级，可以考虑数组存储，使用时线性扫描，实现简单。

但刚需功能支持模糊匹配，使用数组的话效果就不太好了。如此来看的话，还是选择实现一个字典树来进行存储。

前缀树定义：

```C
// cli回调钩子函数签名定义
typedef void* (*cli_hook_func)(unsigned char argc, char *argv[]);

// 节点类型定义
typedef enum{
    PARAM_POS = 0,      // 直接参数，比如 ping <192.168.0.1> 后者
    PARAM_BOOL,         // 不带值的参数，比如 -i
    PARAM_VALUE,        // 带值的参数，比如 -i 1
}cli_param_type_e;

// cli参数定义
typedef struct{
    char short_name;        // 短名参数，例如 -t -i
    cli_param_type_e type;  // 参数类型
    unsigned char required; // 是否必选
    char *help;             // 帮助信息
}cli_param_t;

// cli前缀树节点定义
typedef struct cli_trie_item_s{
    char *name;                         // cmd中的一个单词，需要动态申请内存
    struct cli_trie_item_s **next;      // next array
    unsigned char size;                 // next array size
    char is_end;                        // 是否为cli的结束节点
    cli_hook_func hook;                 // cli回调，is_end == 1 有效
    cli_param_t *params;                // 参数列表，同上
    unsigned char params_cnt;           // 参数个数
    char *help;                         // help信息，同上
}cli_trie_item_t;

// cli前缀树定义
typedef struct{
    cli_trie_item_t root;   // 根节点，作为哨兵使用
}cli_trie_root_t;
```

需要注意的是，前缀树中只存储非参数的节点，不存储参数。一开始我也想把参数放到前缀树里，但实现的时候发现这样做会导致重复注册时参数被覆盖，或者无法匹配精确命令。因此把参数放在`cli_param_t`数组中，挂在`is_end`节点

设计上各个模块初始化向CLI注册命令，后续修改少，因此使用动态分配所需内存，对运行时影响不大

前缀树需要支持插入、查找、展示，实现上还是比较简单的，这里记录一下本设计的一些原则

- 插入：也就是注册命令，业务模块将命令和参数分开注册
    - 目前支持三种参数指定：
        - 直接参数，比如`ping <ip>`
        - 短名参数，比如`ping -t`
        - 短名参数+值，比如`ping -i 10`
- 执行：查找匹配命令，位于`_cli_trie_match`接口。将剩余部分认定为参数，丢给`_cli_trie_parse_execute`
    - 那么参数后面还有命令的情况呢？我认为这是一种烂设计！应该杜绝。原则上需要把所有参数放在最后面

### CLI流程

CLI需要持续监听用户输入，因此在`cli_init`中创建一个线程用于监听用户输入

输入进来后，处理为标准的cmd格式，在前缀树中查找对应hook执行

CLI是串行执行的，需要等待一条CLI执行完毕，再开始接受用户输入，继续执行，因而采用单个线程就好

目前只有用户输入作为来源，大概率不会有其它输入来源，因此无需消息队列中介

## 代办项

- 目前不支持模糊匹配，CLI数量多了有需求再补充
