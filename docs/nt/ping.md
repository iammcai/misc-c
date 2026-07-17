# ping

实现一个简易的ping工具

## 设计思路

ping使用的是icmp协议，基于ip协议之上。在代码上，我们选择复用arp使用的AF_PACKET+SOCK_RAW+ETH_P_ALL，绕过协议栈从L2开始自行构造，主要是为了练习

如果要高精度测量RTT，还是得专用一个ICMP socket来收发测量

PING的流程如下：

1. 根据接口和dst_ip，获取dst_mac。先查询arp cache中有没有相关记录，没有的话需要构造arp查询一下，失败的话，也就不需要ping了
2. 构造ping包发送，维护一个icmp session，等待session更新
3. 在zcap注册了icmp的过滤器，当收到icmp报文时，检查session并更新
4. 第二步使用定时轮询，session更新后，计算数据，继续进行下一次ping