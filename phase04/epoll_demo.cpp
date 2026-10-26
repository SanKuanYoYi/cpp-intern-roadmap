#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
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
  while (true) {
    // -1 表示无限等待；返回就绪事件个数
    int nready = epoll_wait(epfd, events, 64, -1);
    if (nready < 0) {
      if (errno == EINTR)
        continue;
      perror("epoll_wait");
      break;
    }

    for (int i = 0; i < nready; i++) {
      int fd = events[i].data.fd;
      uint32_t evs = events[i].events;

      if (fd == listenfd) {
        // ---------- listenfd 可读 = 有新连接 ----------
        while (true) { // 循环 accept 到 EAGAIN
          int connfd = accept(listenfd, nullptr, nullptr);
          if (connfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
              break;
            if (errno == EINTR)
              continue;
            perror("accept");
            break;
          }
          set_nonblocking(connfd); // 连接也设非阻塞

          struct epoll_event cev;
          cev.events = EPOLLIN;
          cev.data.fd = connfd;

          if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &cev) < 0) {
            perror("epoll_ctl ADD connfd");
            close(connfd);
          } else {
            printf("[epoll] new conn fd=%d\n", connfd);
          }
        }
      } else {
        // ---------- connfd 可读 = 有数据 / 对端关闭 ----------
        char buf[1024];
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);

        if (n < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK)
            continue;
          if (errno == EINTR)
            continue;
          perror("recv");
          epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
          close(fd);
          continue;
        }
        if (n == 0) {
          printf("[epoll] fd=%d peer closed\n", fd);
          epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
          close(fd);
          continue;
        }
        buf[n] = '\0';
        printf("[epoll] fd=%d recv %zd bytes: %s\n", fd, n, buf);

        std::string reply = std::string("echo: ") + buf;
        send_all(fd, reply);

        // demo 约定：一条连接处理一次就关
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
      }
    }
  }

  close(epfd);
  close(listenfd);
  return 0;
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
写回。
这是"改文件标志"的标准模板

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


注册 EPOLLIN 表示：当这个 fd 上有数据可读，或者出现某些“可读”状态时通知我。
常见触发场景：
普通 socket 收到数据：接收缓冲区非空，read/recv 能读到数据。
对端关闭连接：read 会返回 0，这也会触发 EPOLLIN。所以读到 0 要关闭连接。
监听 socket 有新连接：监听 fd 注册 EPOLLIN，触发后调用 accept。

注册 EPOLLOUT 表示：当这个 fd 的发送缓冲区有空间，可以写数据时通知我。
常见场景：
普通 socket 发送缓冲区有空间：可以继续 write/send。
非阻塞 connect 完成：连接建立成功或失败后，fd 通常会变成可写，触发 EPOLLOUT。
此时要用 getsockopt(fd, SOL_SOCKET, SO_ERROR, ...) 判断连接是否成功。
写缓冲从满变为可写：之前 send 返回 EAGAIN，等 EPOLLOUT 后再继续发。

不要一直关注 EPOLLOUT
EPOLLIN 通常常驻关注；
EPOLLOUT 按需开启：有数据要发但没发完时再关注，发完后取消关注。
ET 模式下必须读到 EAGAIN / 写到 EAGAIN

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


uint32_t evs = events[i].events;
把"这个 fd
上实际发生了哪些事件"这个位图取出来，存到一个临时变量里，方便后面按位检查。
events[i].events 是内核填的位图（bitmask）——每一位代表一种事件，一个 fd
可能同时发生多种事件。

struct epoll_event {
    uint32_t     events;   // ← 就是这个字段
    epoll_data_t data;
};

events 是一个 32 位无符号整数，每一位（bit）代表一种事件类型。
不是"一个事件"，是"一组事件的组合"。
位图的本质：
events = 0x00000001   →  二进制 0000...0001  →  只置了 bit0  →  EPOLLIN
events = 0x00000005   →  二进制 0000...0101  →  bit0 + bit2  →  EPOLLIN |
EPOLLERR events = 0x00000010   →  二进制 0000...10000 →  bit4         → EPOLLHUP
一个 fd 可能同时发生多个事件——比如"可读 + 对端挂断"、"可读 + 出错"。
所以这个字段是"或"的关系，不是"等于"某一个值
events 字段的类型就是 uint32_t——用同类型接，是最自然的

为什么不直接用 events[i].events，非要存个临时变量？.
events[i].events 太长了——每次写要打 18 个字符；evs 只 3
个字符——循环体内多次检查时，短名字更好读。


关键理解：== 是错的，& 才对
// ❌ 错误：以为 events 只可能是 EPOLLIN
if (events[i].events == EPOLLIN) {
    // 只有"恰好只发生了 EPOLLIN"才进入
    // 如果对端同时关了连接（EPOLLIN | EPOLLHUP），就走不进来！
}
// ✅ 正确：位图用按位与检查
if (events[i].events & EPOLLIN) {
    // 只要 EPOLLIN 这一位被置了，就进入
    // EPOLLIN | EPOLLHUP 也满足
}
按位与的语义："只检查我关心的那一位，其他位不管"——这才是"位图"的用法。


events 数组的每个元素 = 一个"就绪的 fd + 它发生了什么"。
为什么一次会返回"多个"
因为可能有多条连接同时有事。
3 个客户端几乎同时连进来：或者 3 个已有连接同时发数据：
一次 epoll_wait 返回多个事件"的本质——多个 fd 同时就绪。

for 循环里的"每个元素"到底是什么意思
i	events[i].data.fd	events[i].events	含义
0	listenfd	EPOLLIN	有新连接
1	connfd=5	EPOLLIN	5 号连接有数据
2	connfd=6	EPOLLIN | EPOLLHUP	6 号连接有数据，且对端关了
每个 i = 一个"独立的事件"
if (fd == listenfd) 就是"看单子上这一项是哪个柜子"——是监听 socket 就走 accept
分支，是连接 socket 就走 recv 分支。


"多个事件"有两种含义——别混
层次 1：events 数组里有多个元素（多个 fd 就绪）
nready = 3 表示数组前 3 个有效。这是"多个 fd"——for 循环遍历。
层次 2：单个 events[i].events 里有多位（同一 fd 多个事件）
这是"同一个 fd 的多个事件"——用 & 按位检查。

最简单的例子——两个客户端同时发消息
服务器跑着 epoll_demo，两个 tcp_client_basic 几乎同时发 "hello tcp"：
时刻 T0：客户端 A 的数据到达 connfd=5
时刻 T1：客户端 B 的数据到达 connfd=6
时刻 T2：服务器调用 epoll_wait
epoll_wait 返回 nready = 2
events[0] = { fd=5, events=EPOLLIN }    ← A 的数据
events[1] = { fd=6, events=EPOLLIN }    ← B 的数据
for 循环：
i = 0:
    fd = 5
    recv(5) → "hello tcp"
    send(5, "echo: hello tcp")
    close(5)
i = 1:
    fd = 6
    recv(6) → "hello tcp"
    send(6, "echo: hello tcp")
    close(6)


events[i].events 和你注册的 ev.events 的关系
events 字段是"实际发生"——可能比注册的多（EPOLLERR / EPOLLHUP 自动上报）；
可能比注册的"少"——你注册了 EPOLLIN，但这次只发生
EPOLLHUP（比如对端异常关闭，没数据可读）； 所以生产代码要按优先级检查： if (evs
& EPOLLERR) {
    // 错误优先：连接出错了，先处理
    getsockopt(...);   // 看具体错误
    // 通常直接 close
}
if (evs & EPOLLHUP) {
    // 挂断：对端完全关闭，直接 close
    // 注意：EPOLLHUP 和 EPOLLIN 可能同时出现——缓冲区里还有数据要读
}
if (evs & EPOLLIN) {
    // 可读：recv
}
if (evs & EPOLLOUT) {
    // 可写：send pending 数据
}
注意顺序：先 revc读、EPOLLERR，再 EPOLLHUP，最后 EPOLLIN——
因为出错的 fd 的 recv 结果不可信，先处理错误更安全。
或客户端 send() 然后立刻 close() 会 if (evs & EPOLLHUP) 丢了。

注意：demo 取了 evs 但没用——这是代码的一个"预留"——
在真实生产里，evs 会用于多处判断——demo 只处理 EPOLLIN，省了检查。


ev 和 cev 两个 epoll_event 变量详解
ev 是给 listenfd 注册时用的临时配置，cev 是给每个 connfd 注册时用的临时配置
两个变量内容几乎一样（都是 EPOLLIN），分开命名纯粹是为了"代码意图清晰"

ev —— 只出场一次，给 listenfd 注册
作用域：main 函数里，while 循环之前——只构造一次，只注册一次。
cev —— 在 accept 循环里，每个新连接用一次
作用域：while 循环体内——每次新连接都构造一次，注册完就丢。

为什么分成两个变量，而不是一个
①作用域不同——ev 在循环外，cev 在循环内
作用域越窄越好是 C++ 的基本纪律。
② 命名区分
③能用一个变量，改 fd 就行
epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd / connfd , &ev);   // 注册 fd
但代码可读性变差：
④一个"变量被多线程共享"时，是数据竞争——后果不可预测
生产代码可能更倾向：
变体 1：用统一的"注册函数"
变体 2：用 data.ptr 而非 data.fd
demo 不展示


close(fd) 前必须 epoll_ctl(EPOLL_CTL_DEL, fd)
顺序不能反——先 close 再 DEL 会失败（ENOENT），而且已经留下了"幽灵————
旧 fd 的注册信息"寄生"在了新 fd 上——后果不可预测。
*/




/*
AI 拓展：

select、poll、epoll 的区别？
三者都是 I/O 多路复用——同时监听多个
fd，哪个就绪就处理哪个。差异在实现机制和性能特征：

作为了解：
维度	select	poll	epoll
数据结构	fd_set 位图（固定大小）	pollfd 数组（动态）	内核红黑树 +
就绪链表 最大 fd 数	1024（FD_SETSIZE，编译期固定）	无硬上限（受系统 fd
限制）	无硬上限 每次调用数据传递	全量 fd_set 用户态→内核态拷贝	全量
pollfd 数组拷贝	只在注册时拷贝一次（epoll_ctl） 内核扫描	每次 select O(n)
线性扫描所有 fd	每次 poll O(n) 线性扫描	内核回调机制，O(1) 返回就绪链表 返回后
用户要重新 FD_ZERO + FD_SET	要重置 revents	不需要重置（除非关注事件变了）
触发模式	只有水平触发	只有水平触发	支持 LT 和 ET
fd 类型	只支持 socket（fd < 1024）	支持任意 fd	支持任意 fd

一句话：select 是"老式电话簿"，每次要逐页翻；
poll 是"改进的目录"，但没有根本改；
epoll 是"事件订阅系统"，注册一次，数据到了才通知。



// ET 模式下必须循环读到 EAGAIN
while (true) {
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n > 0) {
        // 处理数据，继续读
    } else if (n == 0) {
        break;  // 对端关闭
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // 读干净了
        if (errno == EINTR) continue;
        // 错误
        break;
    }
}

// LT 模式下一次读一部分也行，下次还会通知
ssize_t n = recv(fd, buf, sizeof(buf), 0);
if (n > 0) {
    // 处理数据（下次 epoll_wait 还会报）
}



为什么 epoll 比 select 快？
① 数据拷贝：select 每次全量拷贝，epoll 只拷贝一次
注册一次，后续零拷贝——这是 epoll 的第一个性能优势。
② 内核扫描：select 是 O(n) 轮询，epoll 是 O(1) 取就绪
epoll 内核实现：注册时，每个 fd 挂一个回调函数
数据到达 → 内核直接回调 → 把 fd 加入就绪链表；
epoll_wait 只需读就绪链表——O(就绪数)。
③ 无 fd 数量限制
select 的 FD_SETSIZE = 1024（编译期固定，改它要重新编译 libc）；
epoll 无硬上限——只受系统 ulimit -n 限制。



demo 用的是 LT 还是 ET？为什么？
用的是 LT（水平触发）
//ev.events  = EPOLLIN;           // ← 没有 EPOLLET 标志
//ev.data.fd = listenfd;
EPOLLIN 不带 EPOLLET 就是 LT。如果要用 ET，必须显式写 EPOLLIN | EPOLLET。

为什么 demo 选 LT
① 代码最简单
② 容错性好
demo 应该先保证正确，再谈优化。
demo 是 recv → 回复 → 关闭"——只处理一次。LT 已经足够，ET 的"高效"在这个场景毫无意义。
生产环境常用 ET
*/


/*
# 终端 1
./epoll_demo
# [epoll] listening on 0.0.0.0:8080 ...

# 另一个终端，跑两次客户端
./tcp_client_basic
./tcp_client_basic
*/

/*
[epoll] new conn fd=5
[epoll] fd=5 recv 9 bytes: Hello TCP
[epoll] new conn fd=5
[epoll] fd=5 recv 9 bytes: Hello TCP

每条连接处理完立即 close——这是"一个连接一个请求"的语义——fd 用完就归还。
所以每一次 accept 拿到的都是"当前最小可用的 fd"——如果前面的都关干净了，就永远是同一个。

假设改成"处理完不 close"：// 错误示范（fd 泄漏）
fd 增加
*/