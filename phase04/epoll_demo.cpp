#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

// 循环发完所有字节
static bool send_all(int fd, const std::string &data) {
  ssize_t total = 0;
  while (total < data.size()) {
    ssize_t n =
        send(fd, data.data() + total, data.size() - total, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (n == 0)
      return false;
    total += n;
  }
  return true;
}

static void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0)
    return;
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
  // ---------- 1. 监听 socket ----------
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    perror("socket");
    return 1;
  }

  int opt = 1;
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(8080);

  if (bind(listenfd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) <
      0) {
    perror("bind");
    close(listenfd);
    return 1;
  }

  if (listen(listenfd, 128) < 0) {
    perror("listen");
    close(listenfd);
    return 1;
  }

  set_nonblocking(listenfd); // accept 能循环到 EAGAIN

  // ---------- 2. 创建 epoll 实例 ----------
  int epfd = epoll_create1(0);
  if (epfd < 0) {
    perror("epoll_create1");
    close(listenfd);
    return 1;
  }

  // ---------- 3. 把 listenfd 注册进 epoll ----------
  struct epoll_event ev;
  ev.events = EPOLLIN;   // 关心"可读"事件
  ev.data.fd = listenfd; // 事件发生时带回这个 fd
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev) < 0) {
    perror("epoll_ctl ADD listenfd");
    close(epfd);
    close(listenfd);
    return 1;
  }

  printf("[epoll] listening on 0.0.0.0:8080 ...\n");

  struct epoll_event events[64]; // 一次最多取 64 个就绪事件

  // ---------- 4. 事件循环 ----------
}

/*
AI 解析文档：

epoll 是什么
epoll 是 Linux 提供的一个"事件通知系统" ——你告诉它"我关心这些
fd（文件描述符，比如网络连接）上有没有数据来"，
它就帮你盯着，哪个有数据了就叫醒你，你不用自己挨个问。
核心价值：一个线程 + 一个 epoll 实例 =
同时盯住几万个连接，哪个连接有数据就处理哪个。不用开几万个线程。

三个系统调用
调用	干什么	快递柜比喻
epoll_create1()	创建一个 epoll 实例	找物业开个"代收服务"账户
epoll_ctl(ADD)	注册一个 fd，告诉 epoll "我关心它"	告诉物业"3
号柜也帮我盯着" epoll_wait()	阻塞等待，直到有 fd 就绪
问物业"有我的快递吗？"（没有就等着）


LT 是什么 —— "有数据就一直提醒你"
LT = Level Triggered（水平触发），epoll 的默认模式。
核心："柜子里有东西"这个状态只要成立，就一直通知你。
LT 的特点：
✅ 没读完，下次还提醒 —— 容错，不怕漏读；
✅ 编程简单 —— 不用非要一次读完；
❌ 可能重复提醒 —— 同一份数据提醒多次，多几次 epoll_wait 返回。


ET 是什么 —— "来新数据才提醒一次"
ET = Edge Triggered（边缘触发），需要显式加 EPOLLET 标志。
核心：只在"从没有到有"这个变化的一瞬间通知一次。之后不管柜子里还有多少，都不再喊。
ET 的致命陷阱：如果你第一次没读干净，剩下的数据不会再有第二次通知 ——
连接假死（数据在，但没人处理）。 ET 的特点： ✅ 通知次数少 ——
同一批数据只提醒一次，epoll_wait 返回次数少，性能略好； ✅ 适合配合"循环读到空"
—— 既然只提醒一次，就得一次读干净； ❌ 必须非阻塞 + 循环读 —— 否则漏读 =
数据永久滞留； ❌ 编程复杂 —— 漏一个 EAGAIN 判断就出 bug。



set_nonblocking() 详解 —— 把 fd 切换到"非阻塞模式"
用 fcntl 拿到 fd 当前的文件状态标志，加上 O_NONBLOCK 位，再写回去。
之后这个 fd 上的 accept/recv/send 都不再阻塞——没数据就立刻返回 -1, errno =
EAGAIN。

① fcntl(fd, F_GETFL, 0) —— 读出当前标志
fcntl 是"file control"——对打开的文件描述符做各种控制操作

它的命令（第二个参数）很多：
命令	作用
F_GETFL	读文件状态标志（File status FLags）
F_SETFL	写文件状态标志
F_GETFD / F_SETFD	读/写 fd 标志（FD_CLOEXEC 等）
F_DUPFD	复制 fd
F_SETLK / F_GETLK	文件锁

F_GETFL 返回的是"这一组标志的位图"，典型的位包括：
标志	含义
O_RDONLY / O_WRONLY / O_RDWR	读写权限（这个不能用 F_SETFL 改）
O_APPEND	每次写追加到末尾
O_NONBLOCK	非阻塞模式
O_ASYNC	异步 I/O 通知（SIGIO）
O_DIRECT	绕过页缓存
F_GETFL 的第三个参数——F_GETFL 不需要参数，传 0 是习惯

② if (flags < 0) return; —— 失败就不动
F_GETFL 失败（EBADF、EINVAL）时返回 -1。不加这个判断，后面会拿 -1
去做位或，写出垃圾值 注意 set_nonblocking 返回值是 void——失败静默返回。这是 demo
的简化—— 生产代码应该返回 bool，让调用方决定是否继续。 代码里 set_nonblocking
失败意味着"这个 fd 还是阻塞的"—— 后面 recv 可能把整个 epoll
循环卡死，所以生产代码必须检查

③ fcntl(fd, F_SETFL, flags | O_NONBLOCK) —— 加上非阻塞位写回去
flags | O_NONBLOCK：在原有位上追加 O_NONBLOCK，保留其他标志；
F_SETFL：用新值替换文件状态标志。

为什么必须 F_GETFL 再 F_SETFL，不能直接 fcntl(fd, F_SETFL, O_NONBLOCK)？
因为 F_SETFL 是"整体替换"，不是"追加位"。直接传 O_NONBLOCK 会清掉所有其他标志——
比如 fd 原来是 O_RDWR | O_APPEND，传进去之后就只剩 O_NONBLOCK
了，读写权限、追加语义全丢 正确姿势：先读原值 → 修改某一位 →
写回。这是"改文件标志"的标准模板：

同理取消非阻塞：
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);   // 清掉这一位



非阻塞 = "不等，立刻返回"
阻塞模式下：
ssize_t n = recv(fd, buf, len, 0);
// 缓冲区没数据 → 进程睡眠，等数据
// 等到数据或对端关闭才返回

非阻塞模式下：
ssize_t n = recv(fd, buf, len, 0);
// 缓冲区没数据 → 立刻返回 -1，errno = EAGAIN / EWOULDBLOCK
// 有数据 → 立刻返回实际字节数
// 对端关闭 → 返回 0


为什么 epoll 必须非阻塞
这才是理解 set_nonblocking 的关键——epoll 和阻塞 fd 一起用可能出严重问题。
场景 1：listenfd 必须非阻塞——否则 accept 卡死
为什么循环 accept？
一次 accept 只取一个，剩下的下次才处理。循环 accept 到 EAGAIN 能一次取干净。
如果 listenfd 是阻塞的——最后一次 accept 会卡住！因为连接取完了，但阻塞 accept
不返回，永远等下一个客户端。 整个 epoll 循环直接停摆，所有已经注册的连接都饿死
accept 要有办法"知道取完了"，那就是返回 EAGAIN。

场景 2：connfd 必须非阻塞——LT 下读不干净会重复唤醒，ET 下直接卡死
ET 下必须"循环读到 EAGAIN"——而 EAGAIN 只有非阻塞 fd 才会返回。
阻塞 fd 在缓冲区空时会卡住等数据——ET 下这等于"读空了 → 永远阻塞 →
整条连接卡死"。
"读干净"的标志是什么？——recv 返回 -1 且 errno == EAGAIN。
"EAGAIN"就是"内核说：现在真的没数据了"。

场景 3：send 也可能阻塞
大文件或对端接收窗口小：
非阻塞：返回 EAGAIN，可以先攒起来，等 EPOLLOUT 事件再发。
demo 里响应小不会触发，但生产服务器必须处理。

EPOLLOUT 是什么
EPOLLOUT = "这个 fd 现在可以写数据了"——发送缓冲区有空间，send 不会阻塞。
非阻塞 + EPOLLOUT 的做法（正确但复杂）:
发不完的数据先存起来（pending 缓冲区），不要阻塞等
注册 EPOLLOUT，告诉内核"这个 fd 可写时通知我"；
事件循环继续跑，处理其他连接；
收到 EPOLLOUT 时，继续从 pending 里发剩余数据；
发完了就 去掉 EPOLLOUT——否则 LT 下会一直报"可写"，空转

为什么 demo 不用 EPOLLOUT
"echo: " + buf，最多 1KB 左右——远小于内核发送缓冲区
send 永远不会返回 EAGAIN
生产代码处理大响应必须走 EPOLLOUT 路径。

set_nonblocking函数调用了几次、在哪调用
// ① listenfd
set_nonblocking(listenfd);   // accept 循环需要 EAGAIN
// ② 每个新 accept 出来的 connfd
int connfd = accept(...);
set_nonblocking(connfd);     // recv 循环需要 EAGAIN
//connfd 也要设——accept 出来的 fd 默认是阻塞的（即使 listenfd 是非阻塞的）！
// accept 只复制 fd，不复制标志。

也可以socket/accept4设置--demo无演示

// 设置非阻塞，保存原来的标志
int saved_flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, saved_flags | O_NONBLOCK);
// ... 用非阻塞模式做事情 ...
// 恢复原来（比如想切回阻塞模式做一次同步 recv）
fcntl(fd, F_SETFL, saved_flags);
saved_flags 是"原样"——恢复时直接传它,不需要 & ~O_NONBLOCK


int epoll_create1(int flags);
返回值	语义
>= 0	epoll 实例的 fd（一个普通文件描述符）
-1	失败，errno 被设置

flags 参数——为什么是 0
epoll_create1 只有一个 flags 参数，只有一个合法标志：
标志	作用
0	无特殊行为
EPOLL_CLOEXEC	fd 带 FD_CLOEXEC——exec 系列函数执行新程序时自动关闭
代码里传 0——简单，不带 CLOEXEC。生产环境推荐 EPOLL_CLOEXEC：

和老的 epoll_create 的区别
// 老接口（Linux 2.6，已废弃）
int epoll_create(int size);
参数 size 是什么？——"我大概要监听多少个 fd"。
听起来很重要？其实不是——内核根本不看这个参数。

错误处理——perror + close(listenfd) + return 1
epfd 是 -1，没东西可关——别 close(epfd)，close(-1) 会报 EBADF
listenfd 是之前 socket() 创建的，已经成功——这里失败必须回收，否则 fd 泄漏；

epoll_create1 可能的 errno
errno	含义	场景
EINVAL	flags 非法	传了不认识的标志（比如 EPOLL_CLOEXEC 拼错）
EMFILE	本进程 fd 用光	ulimit -n 到了，之前的 fd 泄漏
ENFILE	系统全局 fd 用光	极端情况，系统级 fd 耗尽
ENOMEM	内存不足	极端情况



struct epoll_event 是什么
struct epoll_event {
    uint32_t     events;   // 4 字节：关注/发生的事件位图
    epoll_data_t data;     // 8 字节：用户数据（union）
};                         // 总共 12 字节（有对齐，实际 sizeof 通常 12）
字段	作用	注册时	返回时
events	事件位图	你填（告诉内核关心什么）
内核填（告诉你什么发生了） data	用户私有数据	你填（随便放，内核不动它）
内核原样带回

ev.events = EPOLLIN
// 关心"可读"事件

对不同类型的 fd，"可读"的含义不同：
fd 类型	EPOLLIN 表示
listenfd	有新连接可 accept（accept 队列非空）
connfd	有数据可 recv（接收缓冲区非空）
pipe 读端	管道里有数据
语义上都是"该读它了"，但处理动作不同（accept vs recv）。区分靠 if (fd ==
listenfd)

ev.data.fd = listenfd —— 最重要的"回来"机制
typedef union epoll_data {
    void    *ptr;    // ① 指向自定义结构体的指针（生产最常用）
    int      fd;     // ② 直接放 fd（demo 用）
    uint32_t u32;    // ③ 放个整数
    uint64_t u64;    // ④ 放个长整数
} epoll_data_t;
ev.data.fd = listenfd;   // 事件发生时带回这个 fd

为什么 data 是 union 而不是 struct
union
同一时间只有一个"活跃成员"——因为内核只需要"标签"本身，不关心它被解释成什么类型

demo 用 data.fd —— 最简单
生产用 data.ptr —— 最灵活


epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev) 四个参数
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);

参数	含义	代码里的值
epfd	epoll 实例	epoll_create1() 返回的那个
op	操作类型	EPOLL_CTL_ADD
fd	被操作的目标 fd	listenfd
event	事件配置	&ev

op 的三个取值
op	作用	event 参数
EPOLL_CTL_ADD	注册新 fd	必须传，告诉内核关心什么事件
EPOLL_CTL_DEL	移除 fd	忽略（Linux 上可以传 NULL，也可以传非 NULL 但会被忽略）
EPOLL_CTL_MOD	修改 fd 关注的事件	必须传，新的事件集合

要"记住"的坑：
ADD 一个已存在的 fd → EEXIST——不能重复 ADD，要改事件用 MOD；
DEL 一个不存在的 fd → ENOENT——要确保先 ADD 过；
MOD 一个不存在的 fd → ENOENT——同上。

epoll_ctl 可能的 errno：
errno	含义
EBADF	epfd 或 listenfd 无效
EEXIST	fd 已注册过（重复 ADD）
EINVAL	epfd 不是 epoll fd，或 op 非法
ENOENT	fd 没注册过（DEL/MOD 时）
ENOMEM	内存不足
EPERM	fd 不支持 epoll（比如普通文件在某些内核）


int epoll_wait(int epfd, struct epoll_event *events,
               int maxevents, int timeout);
参数	含义	代码里的值
epfd	epoll 实例 fd	epoll_create1() 返回的
events	输出缓冲区，内核把就绪事件填进来	events[64]
maxevents	最多填多少个	64
timeout	最多阻塞多久（毫秒）	-1（永久）

返回值	语义
> 0	就绪事件个数——events[0..n-1] 有效
0	超时（只有 timeout > 0 才可能）
-1	错误，看 errno（EINTR 要重试）

安全写法：
struct epoll_event events[64];
constexpr int MAXEV = sizeof(events) / sizeof(events[0]);
int n = epoll_wait(epfd, events, MAXEV, -1);   // 自动匹配，改数组大小不用改参数

timeout	行为	典型用途
-1	永久阻塞，直到有事件或信号	纯事件驱动（demo 用的）
0	立即返回——有就返回，没就返回 0	轮询模式
> 0	最多等 N 毫秒，超时返回 0	需要定时任务时（检查连接超时等）


struct epoll_event events[64] —— 一次取 64 个就绪事件
epoll_wait 把"当前就绪的 fd"填充进这个数组，返回就绪的数量。
nready 告诉你有几个事件，events[0..nready-1] 是具体内容
64 是"经验值"：
生产用 std::vector<epoll_event>，size 动态——或者固定 256/512；
注意 nready 可能为 0——epoll_wait 超时返回 0


events[0..nready-1] 有效——严格只遍历 nready 个：
只遍历到 nready
events[nready..63] 是"上次或更早遗留"的——epoll_wait 不保证清空——读它们就是读垃圾


listenfd 的 ev 和 connfd 的 ev 别复用同一个
struct epoll_event ev;    // listenfd 用
struct epoll_event cev;   // connfd 用
其实可以复用——因为 epoll_ctl
注册完就丢了——但用不同变量名让代码更清晰——一眼看出"这是给连接用的"。

*/