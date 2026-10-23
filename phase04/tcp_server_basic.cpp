/*
AI 先行解析提示：

基础流程：一次完整的“通话”
服务器端就像总机，先准备好“电话线”并“监听”来电；客户端则主动“拨号”。

服务器端流程：
socket() → bind() → listen() → accept() → recv() → send() → close()
socket()：创建一个通信端点（买一部电话）。
bind()：将端点绑定到特定的IP地址和端口（给电话分配一个号码）。
listen()：开始监听，准备接受连接（打开电话的铃声）。
accept()：阻塞等待，直到有客户端连接进来，
成功后返回一个新的套接字用于通信（电话铃响，拿起听筒，建立专门的通话线路）。
recv() / send()：在这个新连接上接收和发送数据（进行对话）。
close()：关闭连接（挂断电话）。
*/
#include <cerrno>
#include <cstdio>
#include <cstring>      //memset
#include <fcntl.h>      //  open
#include <netinet/in.h> //IPv4 专用地址结构 + 字节序
#include <string>
#include <sys/socket.h> //socket 核心 API + 通用地址结构 + 地址族
#include <sys/types.h>
#include <unistd.h> //  close

int main() {

  // ---------- 1. socket() ----------
  // AF_INET=IPv4, SOCK_STREAM=TCP, protocol=0 表示由前两个参数自动推导
  // ── 创建空壳 ──
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    perror("socket");
    return 1;
  }

  // 端口复用：避免服务器重启时 bind 报 "Address already in use"
  // ── 拨开关：允许占用 TIME_WAIT 端口 ──
  int opt = 1;
  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt");
  }

  // ---------- 2. bind() ----------
  struct sockaddr_in servaddr; // 只是在栈上分配了 16 字节的未初始化内存
  memset(&servaddr, 0, sizeof(servaddr)); // 逐个字节填 0。
  servaddr.sin_family = AF_INET;
  // 告诉内核："这个地址是 IPv4 的"
  // 填错成 AF_INET6 但 sockfd 是 IPv4 的 // 填错成 0 → bind 报 EINVAL
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  // #define INADDR_ANY ((in_addr_t) 0x00000000) 就是整数 0，代表地址 0.0.0.0
  //"本机所有网卡上的这个端口都接客"
  // 什么时候用具体地址？多网卡服务器想区分内外网服务时（比如 8080
  // 只在内网网卡开，公网网卡不开），或者本机调试时防止外部误连
  servaddr.sin_port = htons(8080);
  // htons：主机字节序 → 网络字节序（大端）
  // TCP/IP 协议规定所有多字节整数用大端（网络字节序）。
  // x86/ARM 是小端，如果不转，端口 8080（0x1F90）写进内存会变成 0x901F，
  // 内核按大端读出来就成了端口 36895，netstat 会看到监听在 36895 而不是 8080

  // 强转成通用 sockaddr*，告诉内核这个结构体有多大
  if (bind(listenfd, reinterpret_cast<struct sockaddr *>(&servaddr),
           sizeof(servaddr)) < 0) {
    perror("bind");
    close(listenfd);
    return -1;
  };

  /*
  htons	host to network short（16 位）	sin_port
  htonl	host to network long（32 位）	sin_addr.s_addr
  端口 16 位，IP 32 位，所以函数不同。
  sin_addr 是 4 字节用 htonl，sin_port 是 2 字节用 htons
  */

  // ---------- 3. listen() ----------
  // 不阻塞！只是告诉内核"这个 socket 开始接收连接了"
  if (listen(listenfd, 128) < 0) {
    perror("listen");
    close(listenfd);
    return 1;
  }
  printf("[server] listening on 0.0.0.0:8080 ...\n");
  // netstat -tunlp | grep 8080  //  ss -tlnp | grep 8080

  // ---------- 4. accept() ----------
  // 阻塞等待。被信号打断时返回 -1 且 errno == EINTR，要重试
  int connfd = -1;
  do {
    connfd = accept(listenfd, nullptr, nullptr);
  } while (connfd < 0 && errno == EINTR);

  if (connfd < 0) {
    perror("accept");
    close(listenfd);
    return -1;
  }
  printf("[server] client connected, connfd=%d\n", connfd);

  // ---------- 5. recv() ----------
  char buf[1024];
  ssize_t n = recv(connfd, buf, sizeof(buf) - 1, 0);
  if (n < 0) {
    perror("recv");
    close(connfd);   // 关闭这条连接
    close(listenfd); // 停止监听
    return 1;
  }

  if (n == 0) { // 返回 0 不是错误，是对端关闭了连接
    printf("[server] peer closed\n");
    close(connfd);
    close(listenfd);
    return 0;
  }

  buf[n] = '\0';
  printf("[server] recv %zd bytes: %s\n", n, buf);

  // ---------- 6. send() ----------
  std::string replay = std::string("echo: ") + buf;
  ssize_t sent = send(connfd, replay.data(), replay.size(), 0);
  if (sent < 0) {
    perror("send"); // 严格说 send 可能部分发送，需要循环；
  } else {          // demo 小字节以内一般一次发完，这里从简
    printf("[server] sent %zd bytes\n", sent);
  }

  // ---------- 7. close() ----------
  close(connfd);   // 挂断这条连接（发 FIN，四次挥手开始）
  close(listenfd); // 停止监听

  return 0;
}

// 以下为 AI 文档资料，供于查看：
/*
Socket
fd（套接字文件描述符）是用于标识网络套接字的整数句柄，用于在程序中进行网络通信。

int socket(int domain, int type, int protocol);
资料查询粘贴 ：

domain（协议族 / 地址族）   AF_ = Address Family
值	协议族	地址结构	典型场景
AF_INET	IPv4	sockaddr_in（4 字节 IP + 2 字节端口）	绝大多数网络服务
AF_INET6	IPv6	sockaddr_in6（16 字节 IP + 2 字节端口 + scope_id）
IPv6 服务 AF_UNIX / AF_LOCAL	本机	sockaddr_un（文件路径）
本机进程间通信（IPC），无网络开销 AF_PACKET	链路层	sockaddr_ll
抓包（tcpdump）、原始以太帧

type（套接字类型 / 通信语义）
值	语义	承载协议	特点
SOCK_STREAM	面向连接、可靠、有序字节流	TCP
保证顺序、无丢失、无重复，但没有消息边界 SOCK_DGRAM	无连接、不可靠数据报
UDP	保留消息边界，但不保证到达/顺序 SOCK_RAW	原始套接字	自定义
自己构造 IP 头，需要 root SOCK_SEQPACKET	面向连接 + 保留边界	SCTP
Linux 少用 还可以按位或附加标志

protocol（具体协议）
为什么要第三个参数？因为 domain + type 有时不足以唯一确定协议：
AF_INET + SOCK_STREAM → 只能是 IPPROTO_TCP，写 0 让内核自动推导
AF_INET + SOCK_DGRAM → 只能是 IPPROTO_UDP，写 0
AF_INET + SOCK_RAW → 必须显式指定是 IPPROTO_ICMP、IPPROTO_TCP
还是别的，否则内核不知道该造什么 AF_PACKET + SOCK_RAW → 也需要指定（全部以太网帧
or 某一种） 所以规则是：能唯一确定的写 0，不能确定的显式指定。日常写 TCP/UDP
服务器，第三个参数永远填 0

socket() 不是"连上了网"，它只是在内核里注册了一个空壳
用户层看到的只是 int fd = 3，但背后有 struct file → struct socket → struct sock
三层结构。 此时 socket 处于 CLOSED
状态，还没有本地地址、没有对端、没占任何端口—— 所以 netstat 里根本看不到它。

联系 计算机网络：
socket()   →  CLOSED
bind()     →  CLOSED + 本地地址
listen()   →  LISTEN
accept()   →  派生 ESTABLISHED（新 socket）
connect()  →  SYN_SENT → ESTABLISHED
*/

/*
int setsockopt(int sockfd, int level, int optname,
               const void *optval, socklen_t optlen);

资料查询粘贴 ：

参数	含义
sockfd	目标 socket 的 fd（监听或连接 socket 都行，具体看选项）
level	选项所属的协议层级
optname	选项名字
optval	指向选项值的指针（类型由选项决定）
optlen	选项值的大小（字节数）

为什么 optval 是 void*：
因为不同选项的值类型千差万别——int（开关）、struct linger（关闭行为）、struct
timeval（超时）、char[]（接口名）…… 只能用 void* + optlen 让调用方自己保证正确性

level	头文件	管什么	典型选项
SOL_SOCKET	<sys/socket.h>	套接字通用层（与协议无关）
SO_REUSEADDR、SO_KEEPALIVE、SO_RCVBUF、SO_SNDBUF、SO_LINGER、SO_BROADCAST
IPPROTO_TCP	<netinet/tcp.h>	TCP 专用
TCP_NODELAY、TCP_KEEPIDLE、TCP_KEEPINTVL、TCP_KEEPCNT、TCP_MAXSEG IPPROTO_IP
<netinet/in.h>	IP 层	IP_TTL、IP_TOS、IP_MULTICAST_TTL IPPROTO_IPV6
<netinet/in.h>	IPv6 层

为什么值用 int
C 里没有 bool，选项开关约定用 int：1 开、0 关。
虽然某写成 int 是最通用最安全的做法

它到底"复用"什么？
① 让 bind() 能绑定一个处于 TIME_WAIT 的端口（90% 的用途）
TIME_WAIT 是 TCP 四次挥手后的最后一个状态，主动关闭方会停留 2*MSL（Linux
硬编码约 60 秒） bind 会失败？因为内核看端口 8080 被一个 TIME_WAIT 的 socket
占着（它还在等"万一对面重发 FIN"）。 设置 SO_REUSEADDR 后，内核允许新的 bind
抢占这个 TIME_WAIT 端口——TIME_WAIT 只是"逻辑占用"，
不是真的还有数据要处理，抢占是安全的
服务器几乎必开它：否则改一版代码重启就要等 60 秒
② 允许多个 socket 分别绑定"通配地址"和"具体地址"的同一端口
// socket A: bind 0.0.0.0:8080
// socket B: bind 192.168.1.10:8080   ← 不开 SO_REUSEADDR 会失败
典型场景：一个主监听 0.0.0.0，另一个专门监听某块网卡做特殊处理。
③ 它不能让两个进程同时 LISTEN 同一端口

为什么必须在 bind() 之前设
因为 bind 时内核就会检查"这个地址端口能否用"。SO_REUSEADDR
改变的是这次检查的策略—— 必须在 bind 之前把开关拨好，bind
之后再设已经晚了，无效但也不报错，更难排查

故意不 return：setsockopt
失败可能因为内核不支持某个选项（嵌入式环境常见），但这不是致命错误
真正的错误交给后面的 bind() 去报
*/

/*
// <netinet/in.h>
struct sockaddr_in {
    sa_family_t    sin_family;   // 2 字节：地址族，必须 AF_INET
    in_port_t      sin_port;     // 2 字节：端口（网络字节序）
    struct in_addr sin_addr;     // 4 字节：IP 地址
    unsigned char  sin_zero[8];  // 8 字节：纯填充，必须清零
};

struct in_addr {
    uint32_t s_addr;             // 4 字节：IP，网络字节序
};

// <sys/socket.h>
struct sockaddr {
    sa_family_t sa_family;   // 2 字节
    char        sa_data[14]; // 14 字节
};                           // 总共 16 字节


关键点：16 字节这个大小不是随便定的。
它必须和通用结构 struct sockaddr 的大小完全一致——因为后面 bind() 要做指针强转，
两种结构体必须"内存对齐且等长"。

sa_data[14] 是个"黑盒"，IPv4 用它装 port + addr + zero，
IPv6（sockaddr_in6，28 字节）用它装其他东西……
所以 sockaddr_in 里的 sin_zero[8] 就是"为了凑够 16 字节"的填充物：
sin_family(2) + sin_port(2) + sin_addr(4) = 8 字节
sockaddr 需要 16 字节，差 8 字节
→ 用 sin_zero[8] 补齐

memset(&servaddr, 0, sizeof(servaddr));
逐个字节填 0。不是可选项，是强制的，原因有两层：
① 把 sin_zero[8] 清零
sin_zero 是填充字段，内核理论上会忽略它
但是：
某些平台/老内核真的会检查这些字节是否为零，非零可能导致 bind 报错或行为异常；
调试时更安全：memset 后结构体内容可预测，出问题好排查；
代码更干净：比逐字段填省事。
② 更重要的：避免未初始化字段带来的"幽灵 bug"

最稳妥：memset + 逐字段赋值；
或者：struct sockaddr_in servaddr = {};（C++ 全零初始化）再赋值——效果一样

bind 调用
bind(listenfd, reinterpret_cast<struct sockaddr *>(&serveraddr),
sizeof(serveraddr)) ① 第一个参数：listenfd 就是前面 socket()
返回的文件描述符。此刻它还是 CLOSED 状态 ② 第二个参数：reinterpret_cast<struct
sockaddr*>(&servaddr) bind 的原型形参类型是通用结构体 sockaddr，不是
sockaddr_in。 用户填好专用的 sockaddr_in（16 字节，字段清晰）
指针强转（只是在类型系统里"换脸"，内存一个字节都不动）
传给 bind 的 sockaddr*（16 字节，黑盒）
内核根据 sa_family 字段，内部再强转回 sockaddr_in 解析
解析出 port 和 addr

为什么标准库不直接让 bind 接受 sockaddr_in*？
因为要同时兼容 IPv4（sockaddr_in）、IPv6（sockaddr_in6，28 字节）...
只能用一个抽象接口。这是 C 语言时代没有泛型/多态的历史包袱

③ 第三个参数：sizeof(servaddr)
为什么要传大小？ 因为内核收到的是 struct sockaddr*，
它不知道实际是哪种协议族的地址（虽然 sa_family 能告诉它，但 sa_family
字段本身也在后面，需要先读一下）。 太小 → EINVAL（结构体不完整） 太大 → Linux
上一般不报错（向后兼容），但语义不严谨 注意传的是
sizeof(servaddr)（具体类型的大小，16 字节），不是 sizeof(struct sockaddr*)。
后者是指针大小（64 位系统是 8），传进去会 EINVAL

④ 错误处理部分
close(listenfd) 为什么不能省？虽然进程退出时内核会回收所有 fd，但
这是个好习惯：写大程序时错误分支多了，漏一个就是 fd 泄漏；
表达意图：明确告诉读者"这个 fd 的生命周期到此结束"；
如果后面还有逻辑（比如重试、切换端口），不关会泄漏


int listen(int sockfd, int backlog);
listen() 不阻塞、不等待、不连接任何客户端。 它真正做的是两件事
状态跃迁：把 sockfd 从 CLOSED 变成 LISTEN；
初始化队列：告诉内核"这个 socket 可以从现在开始收连接请求了，队列长度设为
backlog" 真正的"等待连接"是 accept() 干的

backlog 到底是什么？
backlog 是全连接队列（accept queue）的最大长度，
不是"最多允许这么多并发连接"，也不是"限制总连接数"

一个监听 socket 在 LISTEN 状态下，内核为它维护两个独立的队列
SYN 队列（半连接队列 / 未完成队列）
accept 队列（全连接队列 / 已完成队列）

accept 队列满了：新的完成握手连接被丢弃或拒绝
Linux 默认行为是静默丢弃，靠客户端重传 SYN-ACK
触发重试，看起来就是"连接很慢"或"超时"） SYN 队列满了：新的 SYN
被丢弃，客户端重传（典型现象：connect() 卡住几秒）

128 是个经验值，够 demo 用。真相是：
// 内核实际生效值
实际 backlog = min(你传的值, /proc/sys/net/core/somaxconn)

为什么错误时也要 close？
进程退出后内核会回收所有 fd，但显式 close 是好习惯；
写多分支错误处理时容易漏，每次失败都 close 是纪律

close 一个从未 listen 的 socket 也完全正常——它只是释放 fd 及其关联的内核对象。
内核不要求你"先 listen 再 close"

listen 是不是真的"开始接收连接"？
是——但接收的是内核在做，不是应用在做。
应用还没 accept 时，内核已经在收 SYN、完成握手、把连接堆进 accept 队列了。
所以 listen 之后就算应用一直不 accept，客户端也能 connect 成功（在 backlog
范围内）。 这就是为什么队列会溢出

backlog 传 0 会怎样？
内核会把它设为 min(somaxconn, 1)——即实际至少 1。传 0 不等于"不允许连接"

UDP 上能 listen 吗？
不能。listen 返回 EOPNOTSUPP。UDP 是无连接协议，不需要 listen，bind 完直接
recvfrom 错误很典型：把 TCP 代码改成 UDP 忘了删 listen。


int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
参数	作用
sockfd	监听 socket（LISTEN 状态）
addr	出参，回填对端地址；不关心传 nullptr
addrlen	in/out 出参，进来是 addr 缓冲区大小，出去是实际写入大小
与 recv 不同：recv 返回 0 表示对端关闭，accept 不返回 0
所以不要写 if (accept(...) == 0)
accept 的 addr/addrlen 是出参（和 bind
的入参不同），用来回填"这个连接是谁连进来的"

EINTR 是什么
假设服务器正在 accept() 阻塞等待，此时用户按了 Ctrl+C（或者别的进程发了信号）
信号处理是操作系统"打断系统调用"的机制。accept、recv、select、sleep、wait
等阻塞系统调用都可能被信号打断， 返回 -1 并设置 errno = EINTR 这不是错误！
只是"系统调用的正常中止"，应该重新调用

哪些 errno 该重试
errno	含义	重试？
EINTR	被信号打断	✅ 立即重试
EAGAIN/EWOULDBLOCK	非阻塞模式下暂无连接	⏳ 稍后再试（通常是事件循环）
ECONNABORTED	连接被对端中止（三次握手后断了）	⚠️ 通常忽略，继续 accept
其他通常放弃，为bug  (还要判断 connfd < 0)
do...while 语义更清晰："先做一次，失败就重试"，这是重试逻辑的天然表达。

accept 只"取连接"，不"等连接" ，不是"建立连接"
accept 队列非空 → 立即取队头，返回 connfd
accept 队列为空 → 阻塞，等内核通知"有新连接进入队列"

accept 返回的 fd 是全新 fd，和 listenfd 无关


ssize_t recv(int sockfd, void *buf, size_t len, int flags);
sockfd	连接 socket（connfd），不是 listenfd
buf	接收缓冲区（用户态内存）
len	缓冲区大小，最多收这么多字节
flags	行为标志，0 = 常规阻塞接收
返回值是 ssize_t（有符号），因为要表达"负数错误"：

返回值	语义	处理
> 0	实际收到的字节数（可能小于 len）	处理这 n 字节
0	对端正常关闭连接（收到 FIN）	关闭 connfd
-1	错误	看 errno 决定
recv 返回 n > 0 时，它只保证"拿到了 n 字节"，不保证"这就是对端发出的全部"。
对端可能发了 1000 字节，recv 只返回 200

ssize_t n = recv(connfd, buf, sizeof(buf) - 1, 0);
为什么传 sizeof(buf) - 1 而不是 sizeof(buf)
因为下一行要 buf[n] = '\0'——给字符串终止符留一个位置
如果传 sizeof(buf)（1024），对端正好发 1024 字节，recv 返回 n=1024，
再 buf[1024]='\0' 就越界写（数组下标 0~1023）

recv 失败不等于"连接挂了"
errno 决定处理方式：
errno	含义	处理策略
EINTR	被信号打断	✅ 应该重试（这里没有，见 3.3）
EAGAIN/EWOULDBLOCK	非阻塞模式下暂时无数据	⏳ 稍后再试（事件循环）
其他可能是bug

为什么这里同时关 connfd 和 listenfd
因为是"接受一个连接就退出"的 demo。
生产服务器不会这样——recv 出错只关这条连接，listenfd 继续用（循环 accept
下一个客户端） 顺序很重要：先关 connfd 再关 listenfd，虽然 demo
里没区别，但语义上先断业务连接再停监听，逻辑更顺。 真实服务器里 connfd 会被
close，listenfd 是程序末尾才关

这里没有 EINTR 重试
recv 也可能返回 EINTR，代码里没有 do...while 处理。原因
demo 的 SIGINT 默认直接杀进程，走不到 errno == EINTR 分支
单连接 demo，出错就直接失败退出了
但生产代码必须重试：
ssize_t n;
do {
    n = recv(connfd, buf, sizeof(buf) - 1, 0);
} while (n < 0 && errno == EINTR);

recv == 0：对端正常关闭（发了 FIN），数据已完整收完；
必须 close，不能循环等待
recv == -1, errno == ECONNRESET：对端强制断开（发了 RST），可能数据丢

为什么要手动加 \0
recv 不会帮你在数据末尾加 \0——它只是把字节复制到 buf，像 memcpy 一样

如果收的是二进制协议（比如 protobuf 消息、图片数据）
二进制数据里可能包含 0x00，\0 之后的内容会被 printf 丢弃
用长度显式处理
for (ssize_t i = 0; i < n; ++i)
    printf("%02x ", (unsigned char)buf[i]);
规则：buf[n]='\0' 只适用于面向字符串/文本协议的场景（HTTP、SMTP、Redis RESP
等）。 二进制协议必须用 (buf, n) 对来处理，绝不依赖 \0

TCP 字节流没有消息边界
假设了一次 recv 就能收到对端一次 send 的全部内容——这是错的，只不过 demo
里恰好能工作 TCP 是字节流协议，不保留消息边界。它只保证： 字节按序到达；
不丢（除非连接异常）；
不重复（内核自动去重）

但不保证：
"发送方几次 send，接收方就几次 recv"；
"一次 recv 对应一次 send"；
"recv 返回的数据量和 send 的数据量相同"

拆包原因：
内核缓冲区满（发送方 send 太快，接收方没及时读）；
MSS（最大报文段）限制，大消息会被拆成多个 TCP 段；
Nagle 算法攒小包；网络 MTU 分片。(了解)
粘包原因：
发送方连续两次 send，内核可能合并成一个 TCP 段发出（Nagle）；
接收方 recv 调得慢，两个段都到了缓冲区，一次读出来

demo 为什么没暴露
对端只发 "hello tcp"（9 字节），远小于 1023；
客户端 send 一次，发完立刻 recv 等回复，不发第二条；
本地回环网络快，几乎不会拆包

正确写法
方案 1：固定长度前缀
方案 2：分隔符
方案 3：定长消息


ssize_t send(int sockfd, const void *buf, size_t len, int flags);
sockfd	连接 socket（connfd），不是 listenfd
buf	要发送的数据指针
len	数据字节数
flags	行为标志，0 = 常规阻塞发送

关键认知：send 返回的是"这次拷进了多少个字节"，不是"对端收到了多少"。
send 是"把数据交给内核"，不是"把数据送到对端"。
send 返回值 < len 是合法的

它只保证"数据进了内核缓冲区"，不保证
数据已经到了对端（可能还在网络里、在内核缓冲区里）；
对端应用已经 recv 了；
len 个字节全部发出去了（部分发送）

std::string reply = std::string("echo: ") + buf;
为什么用 std::string 而不是 char[]
因为要拼接两段内容："echo: " + buf 里的内容

std::string 的好处：
自动管理内存，不用算长度；
+ 运算符天然支持拼接；
data()/size() 直接拿到指针和长度，不依赖 \0
（虽然 std::string 内部会维护 \0，但 send 用的是 size() 不是 strlen）

为什么用 data() 而不是 c_str()
两者在 C++11 之后完全等价。返回以 \0 结尾的 const char*
用 data() 是因为 send 需要的是"数据 + 长度"，不需要 \0 终止——send 只发 size()
个字节，\0 在范围内就发，不在就不发。 data() 语义更贴合"这是二进制数据"

不要用 strlen(reply.c_str())
// ❌ 对二进制数据会截断
send(connfd, reply.c_str(), strlen(reply.c_str()), 0);
// ✅ 用 size()，与内容无关
send(connfd, reply.data(), reply.size(), 0);

strlen 遇到 \0 就停，如果 reply 里有二进制 0x00 会少发。
send 本来就是按长度发，用 size() 才是正解。


flags = 0 的语义
flags 是行为标志，常用值：
0	常规阻塞发送
MSG_NOSIGNAL	对端关闭时不发 SIGPIPE（Linux 专用，非常重要）
MSG_DONTWAIT	本次调用非阻塞
MSG_OOB	发送 TCP 紧急数据
MSG_MORE	内核先攒着，不急着发（配合 TCP_CORK）

生产代码里，flags 几乎必须带 MSG_NOSIGNAL


生产代码必须写循环
// ✅ 正确：循环直到发完
size_t total = 0;
while (total < reply.size()) {
    ssize_t n = send(connfd, reply.data() + total, reply.size() - total,
MSG_NOSIGNAL); if (n < 0) { if (errno == EINTR) continue;           //
信号打断，重试 if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 非阻塞模式：缓冲区满，稍后再试
            continue;
        }
        perror("send");
        close(connfd);
        return -1;
    }
    if (n == 0) break;   // 理论上不会出现，防御性判断
    total += n;
}
// 此时 total == reply.size()，全部发出


代码里 perror 后没 close(connfd)
不是 bug，但是个隐患——send 失败后代码继续往下走，最后统一 close(connfd);
close(listenfd); 生产代码： if (sent < 0) { if (errno == EINTR) { 重试循环 }
    else if (errno == EPIPE) {
        fprintf(stderr, "[server] peer closed before reply\n");
    } else {
        perror("send");
    }
    close(connfd);
    close(listenfd);
    return 1;
}



SIGPIPE——send 这是 send 最大的陷阱，
服务器 send() → 内核发现"对端已关闭"，触发 SIGPIPE
服务器进程被 SIGPIPE 默认动作（终止）杀掉 ❌
SIGPIPE 的默认行为是终止进程。

方案 1：send 带 MSG_NOSIGNAL（推荐，Linux 专用）
send(connfd, buf, len, MSG_NOSIGNAL);
// 对端关闭时只返回 -1, errno=EPIPE，不发 SIGPIPE
只影响这一次调用，不改全局。

方案 2：全局忽略 SIGPIPE（跨平台）
signal(SIGPIPE, SIG_IGN);   // 或 sigaction
在 main 开头加一次，所有 send 都安全

demo 里没处理——
因为客户端发完 "hello" 立刻 recv 等回复，没来得及 close，服务器 send
时对端还在，SIGPIPE 不会触发 但：客户端如果 send 后立刻 close
不等回复，服务器就会被 SIGPIPE 杀死。
*/

/*
listenfd 是"前台接待员"，只负责迎客，不干活；
connfd 是"一对一专属客服"，每个客户一条专线，只负责跟这一个客户说话。

listenfd 不能 recv/send —— 它只承载"连接"这个动作，不承载"数据"。
recv(listenfd, ...) 会报 ENOTCONN。

accept 返回的是新 fd，不是复用 listenfd —— 这是 TCP 服务器能并发的物理基础。
一个 listenfd 派生 N 个 connfd，每个能独立 select/epoll 监听。

close(connfd) 不影响 listenfd，也不影响别的 connfd ——
关一个客户，其他客户照常说话； 只有 close(listenfd) 才是"整体关门歇业"。
*/

/*
AI 拓展提示：

第一梯队：面试必考、项目必处理的 5 个错误
场景：你使用了非阻塞 Socket，调用 read/recv 读数据，或者调用 accept
接新连接时，发现没数据可读或没连接进来。 面试考点：这根本不是真正的“错误”。在
Epoll/Reactor 模型中，发生 EAGAIN 意味着“现在没数据，你先去忙别的，等 Epoll
通知你再来”。你要能说出非阻塞 IO 必须处理这个错误，否则程序就挂了。
2. EINTR（被信号中断）
场景：你的程序在调用阻塞的 accept 或 read 时，突然来了个信号（比如
SIGCHLD、SIGALRM），系统调用被强行打断，返回 -1 并设置 errno 为 EINTR。
面试考点：处理方式。老道的程序员必须把网络 IO 放在一个 while 循环里，如果遇到
EINTR，必须重新调用。如果你不处理，程序可能莫名其妙地报错退出。
3. ECONNRESET（连接被重置，对端发 RST）
场景：比如你正在和服务端保持长连接，但服务端突然宕机、重启，或者你发了数据给一个已经崩溃的进程，操作系统会替你发送一个
RST 包。 面试考点：考察 TCP 状态机。当你收到 ECONNRESET，说明 TCP
收到了对方发来的 RST。这通常意味着对方进程崩溃了，或者对方强制关闭了
Socket（如设置了 SO_LINGER=0）。
4. EPIPE / SIGPIPE（向已关闭的管道写数据）
场景：客户端已经主动断开连接（发了 FIN），但你的服务端不知道，还在傻傻地往这个
Socket 里 write/send 数据。 面试考点：这是必考题！默认情况下，Linux 会发送
SIGPIPE 信号，直接杀死你的进程。标准做法是：在网络编程开头，必须加上
signal(SIGPIPE, SIG_IGN); 忽略该信号，然后通过判断 send 返回的 errno == EPIPE
来处理。如果不处理，你的高并发服务器只要有一个客户端异常断线，整个服务端就崩了。
5. ETIMEDOUT / ECONNREFUSED（超时 / 拒绝）
ECONNREFUSED：你连对方，对方直接给你回了
RST。原因：对方端口根本没开（服务没启动），或者防火墙直接拒绝。对应 TCP 的 SYN
请求没收到 ACK。
ETIMEDOUT：你连对方，对方毫无反应，包发过去石沉大海。原因：网线断了、对方服务器挂了、或者被防火墙直接丢包了（DROP）。对应
TCP 触发了重传机制，重传达到最大次数后放弃。

第二梯队：遇到查文档即可（基本不考）
EADDRINUSE：端口被占用（通常是你写代码时没释放端口或者服务没关）。
EMFILE / ENFILE：文件描述符耗尽（高并发服务器常见，说明你的 ulimit -n
不够，或者没有正确 close 导致泄露）。 EACCES / EPERM：权限不够（比如监听 1024
以下端口）。 EHOSTUNREACH / ENETUNREACH：主机或网络不可达（路由问题）。
*/