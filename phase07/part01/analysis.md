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


### static void mod_events(int epfd, int fd, bool want_write) 
为什么封装成函数？
代码里 mod_events 被调用 3 次（flush 里注册、flush 里注销、未来扩展），每次都要填 5 行 epoll_event。
封装避免重复，也防止某处忘填 ev.data.fd 这种致命 bug

为什么必须动态改，而不是一直开着 EPOLLOUT？
这是 epoll 最经典的性能坑：EPOLLOUT 几乎永远是就绪的。
只要 socket 发送缓冲有空间，内核就认为"可写"。
如果你常驻注册 EPOLLOUT，epoll_wait 每次都会把这个 fd 塞进返回列表，你的主循环疯狂空转，其他连接的 EPOLLIN 被饿死。

关键陷阱：MOD 只能用于已注册的 fd


### std::vector<int> to_close —— 延迟关闭队列
解决的核心问题：遍历容器时不能删除元素
auto it = conns.find(fd);       // it 指向 conns 里的某个节点
Conn& c = it->second;           // c 是引用，绑定到那个节点

// 如果这里直接 conns.erase(fd)：
//   ① it 失效
//   ② c 变成悬空引用（dangling reference）
//   ③ 后续 c.out.empty() / flush(c) 就是 use-after-free

unordered_map::erase 会让被删节点的迭代器和引用失效。
而事件处理代码里到处都是 c.xxx，一旦提前 erase，后面任何一次 c. 访问都是未定义行为

延迟关闭 = "标记删除 + 统一清理"

为什么必须在"本轮所有事件处理完"才关？
因为 epoll_wait 一次可能返回多个事件，
而同一轮里：同一 fd 可能触发多个 event 项 / 不同 fd 之间可能有逻辑关联 / 等等


### if (evs & (EPOLLERR | EPOLLHUP)) —— 错误/挂断优先处理
标志	触发条件	通俗解释
EPOLLERR	该 fd 发生错误（如 EPIPE、ECONNRESET、socket 异常）	"这连接坏了"
EPOLLHUP	对端挂断（full close，收到 FIN 且本地也关了写）	"对端彻底走了"
EPOLLRDHUP（未用）	对端半关闭（只 shutdown 写方向，还能读）	"对端说完了，但还能听我说"

为什么 EPOLLERR/EPOLLHUP 要优先判断，且不管有没有 EPOLLIN？
关键：EPOLLERR 和 EPOLLHUP 是"无条件触发"的。
这是 epoll 的特殊语义：EPOLLERR 和 EPOLLHUP 总是被监听，无需显式注册。
所以 evs 里可能出现 EPOLLIN | EPOLLHUP（数据可读 + 对端挂断），也可能只有 EPOLLHUP。

为什么必须先检查它们、再检查 EPOLLIN？
兜底：EPOLLERR/EPOLLHUP 出现时，不管是否伴随 EPOLLIN，都立刻关连接
优先：先于 read 判断，避免在"已坏"的 fd 上做无意义的 read/write
防泄漏：避免"坏 fd 永远留在 epoll 里空转"的经典 bug

continue 的作用
处理完这个 fd（标记关闭）后，跳过本轮对它的 read/flush，直接处理 events[i+1]。因为：
已经 close_conn 了，再 read 是浪费（虽然延迟关没立刻 close，但语义上已死）
避免在 flush 时对已标记关闭的 fd 写数据

### flush 的语义：
  ┌─ 能发 → 一直发
  ├─ 发不动了（EAGAIN）→ 保存 sent 进度，注册 EPOLLOUT，退出
  └─ 全发完 → 清空 out，注销 EPOLLOUT

关键闭环：flush 成功发完后必须 mod_events(epfd, c.fd, false) 把 EPOLLOUT 摘掉。
否则回到之前说的坑：EPOLLOUT 常驻 → epoll_wait 永远返回这个 fd → CPU 空转。

### conns.find(fd) + end() 检查
events[i].data.fd 是内核在 epoll_wait 返回那一刻填进来的快照。
从"内核填进 events[]"到你"在用户态处理它"，中间隔了一段时间——这段时间里，这个 fd 可能已经在 conns 里被删掉了。

