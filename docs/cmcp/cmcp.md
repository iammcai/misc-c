# CMCP

在有了发包和抓包的轮子后，打算自己实现一套协议用来练习，正好手头有两个树莓派，可以拿来运行测试

协议名字就叫做CMCP，含义是 Cai Misc-C Communicate Protocol

## 协议格式

协议基于自定义的etherType == 0x1CC1，自定义上层格式

```C
// cmcp头部定义
typedef struct{
    uint8_t version;            // 版本号
    uint8_t type;               // bit7：0-请求，1-响应；bit6-0：事务类型
    uint16_t payload_len;       // payload长度
    uint32_t seq;               // 事务序列号
    uint16_t reserved;          // 保留，后续可能扩展为flags
    uint16_t checksum;          // 头部+payload校验和
}attr_packed pkthdr_cmcp_t;
_Static_assert(sizeof(pkthdr_cmcp_t) == 12, "sizeof pkthdr_cmcp_t must be 12 !");
```

## 通信流程

### peer建立

通信的第一步需要建立邻居，使用hello报文来交互

