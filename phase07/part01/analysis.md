<!-->
文档解析，供于查看
<-->


# main_V3.cpp

### test/slow_client.py

slow_client: 疯狂 send 32MB，但一个字节都不 read
                    │
                    ▼
       内核 socket 接收缓冲区（对端）被塞满
                    │
                    ▼
       TCP 滑动窗口收到对端通告 "窗口=0"
                    │
                    ▼
       server 端 write() 发不出去 → 内核发送缓冲区满
                    │
                    ▼
       server 的 write() 在【阻塞 socket】上 → 睡死
                    │
                    ▼
       单线程卡在这一个 write 上 → 整个事件循环停摆
                    │
                    ▼
       其他客户端 nc 连上了但永远不回显


阻塞在哪一层？	
内核 TCP 发送缓冲区。write 把数据拷进内核发送缓冲区，缓冲区满了就阻塞

为什么 nc 能连上却不回显？	
accept 是在上一个慢客户端 write 卡死之前发生的，新连接进了队列；但主线程卡在 write，轮不到 epoll_wait 去处理它

T3 的 epoll 为什么救不了？	
epoll 只管"哪些 fd 可读可写"，但 read 之后的 write 还是阻塞的——炸弹在应用层，不在 epoll


改动 ①：连接 fd 全部非阻塞
改动 ②：read 循环到 EAGAIN（不是读一次）
改动 ③：write 写不完 → 存起来 + 注册 EPOLLOUT
out:  [已经发出去的][还没发出去的]
      └── sent ──┘ └─ out.size()-sent ─┘


















# poller.h

Poller 是 epoll 系统调用的薄封装，只做两件事——
把 Channel 关心的事件同步到内核，以及把内核返回的事件翻译回 Channel

### Poller() : epfd_(::epoll_create1(EPOLL_CLOEXEC)), events_(1024) {}
CLOEXEC 保证后续 fork + exec 时这个 fd 不会泄漏进子进程。
服务器里所有 fd（socket、epoll、eventfd）都该带这个标志

### std::vector<epoll_event> events_(1024);复用
epoll_wait(epfd, events, maxevents, timeout) 的第二个参数是输出缓冲区，内核每次覆写前 n 个元素，不保留旧内容

### 为什么是 unordered_map<int, Channel*> 而不是 Channel* 数组或 data.ptr
data.ptr = Channel*	直接拿指针	Channel 销毁后成野指针；必须严格"先 DEL 再析构"
Channel* channels_[MAXFD]	数组下标	fd 可能很大（ulimit -n 可到百万）；要处理 fd 复用；大数组浪费
data.fd + unordered_map<int, Channel*>	map.find(fd)	一次哈希查找；Channel 销毁后查不到，兜底 continue 即可，最安全

### updateChannel：fd 的增删改
ADD：fd 当前不在 epoll 集合里，否则 EEXIST。
MOD：fd 必须在集合里，否则 ENOENT。
DEL：fd 在不在都能调，ENOENT 可忽略（内核版本差异，稳妥起见先查 map）。

必须先 epoll_ctl 成功，再改 channels_。


