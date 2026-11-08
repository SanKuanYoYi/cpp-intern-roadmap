<!-->
文档解析，供于查看
<-->

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


