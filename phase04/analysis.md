<!-->
此文件粘贴各 demo 的解析、说明等文档，以备需要时阅读
<-->

# tcp_server_basic.cpp

<!-->
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

//struct sockaddr：通用但难用的“老古董”
//定位：通用的套接字地址结构。不仅用于 IPv4，还可以用于 IPv6 等
//它把 IP 地址和端口号全部揉碎了，混在一起塞进了一个 14 字节的 char sa_data[14] 数组里
//它把 IP 地址和端口号全部揉碎了，混在一起塞进了一个 14 字节的 char sa_data[14] 数组里

//struct sockaddr_in：IPv4 专用的“人间清醒版”
//把原本混在一起的 sa_data 拆成了三个清晰的字段：sin_family（家族）、sin_port（端口）、sin_addr（IP 地址）。
//sin_zero[8]：这是个纯占位符。为了让 sockaddr_in 的内存大小和 sockaddr 完全一样（都是 16 字节），从而能完美兼容。
//填数据前必须用 memset 把它清零

//struct in_addr：只负责存 IP 的小零件
//只用来表示 IPv4 地址。只有一个 uint32_t s_addr，也就是一个 32 位无符号整数。
//这完全是 C 语言的历史包袱、类型安全 以及 早期网络设计 共同造成的

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
bind(listenfd, reinterpret_cast<struct sockaddr
*>(&serveraddr),sizeof(serveraddr)) ① 第一个参数：listenfd 就是前面 socket()
返回的文件描述符。此刻它还是 CLOSED 状态
② 第二个参数：reinterpret_cast<structsockaddr*>(&servaddr)
bind 的原型形参类型是通用结构体 sockaddr，不是sockaddr_in。
用户填好专用的 sockaddr_in（16 字节，字段清晰）
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
下一个客户端）
顺序很重要：先关 connfd 再关 listenfd，虽然 demo
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
AI 解析：
1024 是"从 C 传统继承下来的、被证明能用、但不一定是性能最优"的默认值。
如果你写 demo，1024 完全够；如果你写生产代码，请根据消息大小、并发数、是否栈上来选——
4096 通常比 1024 更优，大消息要 64KB+ 并走堆，零拷贝场景直接不用 buf。
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
<-->


# tcp_client_basic.cpp

<!-->

/*
AI 文档粘贴，供于查看

步骤	服务器	客户端
socket()	✅	✅ 一样
bind()	✅ 必须（固定端口 8080）	❌ 不需要（内核自动分配临时端口）
listen()	✅ 必须	❌ 不需要
accept()	✅ 必须	❌ 不需要
connect()	❌ 不需要	✅ 必须（主动连接服务器）
recv/send	✅	✅ 一样（方向相反）


个连接在两个视角下，源/目的端口是反的：
服务器视角：local=8080, peer=54321
客户端视角：local=54321, peer=8080


sockfd 命名差异——纯习惯
// 服务器
int listenfd = socket(...);   // 命名 listenfd，强调"最终会 listen"
// 客户端
int sockfd = socket(...);     // 命名 sockfd，通用名
客户端的那个 fd 就是"我这条连接的 fd"，叫 sockfd 或 clientfd 都行


int inet_pton(int af, const char *src, void *dst);
把人类可读的 "127.0.0.1" 转换成内核能用的 32 位网络字节序整数。
af	地址族：AF_INET（IPv4）或 AF_INET6（IPv6）
src	输入的文本地址，比如 "127.0.0.1" 或 "::1"
dst	出参，写入转换后的 32 位网络字节序整数。类型是
void*，所以传什么进去取决于 af
//p = presentation（文本展示形式，人读的）
//n = network（网络字节序的二进制，内核用的）
还有个反向函数：inet_ntop(af, src, dst, size) —— network 转 presentation，
把 struct in_addr 打回 "192.168.1.10" 字符串。debug 打日志常用。

1	转换成功，dst 里是合法网络字节序整数	成功
0	src 不是合法地址格式（比如 "abc"、"999.1.1.1"）	参数错误
-1	af 参数无效（比如 AF_INET 拼错成 AF_INETX）	系统错误


为什么是 &servaddr.sin_addr 而不是 &servaddr.sin_addr.s_addr
inet_pton 的第三个参数是 void *dst，不是 uint32_t*，它要的是"一个 4
字节容器的地址"。 所以正确的传法有两种，效果完全一样：
// 写法 A（代码里用的）：传 sin_addr 结构体的地址
inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr);
//       ↑  指向 struct in_addr（4 字节）
//       强转成 void*，函数内部写 4 字节进去

// 写法 B：传 s_addr 字段的地址
inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr.s_addr);
//       ↑  指向 uint32_t（4 字节）
//       效果完全相同

为什么两种都对：struct in_addr 只有一个字段 s_addr，所以 &sin_addr 和
&sin_addr.s_addr 是同一个内存地址——inet_pton 往那块内存写 4 字节，
无论按哪个视角解释，都是给 s_addr 赋值。

inet_pton 不接受 struct in_addr* 类型——这就是为什么它用 void*。
因为它要同时服务 IPv4（写 4 字节）和 IPv6（写 16 字节），没法用一个具体类型表达

inet_pton 内部已经做了字节序转换，它输出的 s_addr 就是网络字节序。
再包一层 htonl 会把字节序翻回去，地址就乱套了。


为什么用 fprintf(stderr, ...) 而不是 perror
perror 是"打印 errno 对应的系统错误描述"，
inet_pton 返回 0 时 errno
并不一定被设置——因为"地址格式非法"是用户输入错误，不是系统错误。


int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
sockfd	客户端 socket（socket() 创建，通常没 bind）
addr	目标地址（服务器的 IP + 端口）
addrlen	地址结构体大小

返回值	语义
0	连接建立成功（三次握手完成）
-1	失败，errno 被设

                bind	                connect
addr 代表	我的本地地址（"我在哪"）	对端地址（"我去哪"）
内核动作	登记地址，无网络交互	主动发 SYN，触发三次握手
返回值	0 成功	0 成功
阻塞吗	不阻塞	阻塞（默认同步模式，等三次握手完成）

int connect(int, const struct sockaddr*, socklen_t);
//                        ↑ 形参是通用类型
和 bind 里一样的原因
connect 的形参是 sockaddr*（16 字节黑盒），但用户填好的是
sockaddr_in（有具体字段）。 所以传指针时强转——内存不变，只是让编译器接受。

connect 的形参是 const struct sockaddr*（注意 const），bind 是 struct
sockaddr*（无 const） bind 内核会修改 sockaddr 吗？其实也不会——但历史 API
就写成非 const

服务器没启动时，connect 会立即失败（RST 回得快）；
服务器不响应时，connect 会卡很久。(SYN 发出去没人响应，客户端要重传
SYN，直到超时)


ECONNREFUSED = "Connection refused" = 对端明确拒绝了连接请求。
服务器没启动：8080 端口没进程 LISTEN，内核回 RST；
服务器启动了但没 listen 8080：只有 socket + bind 没 listen，内核不认这个端口，回
RST； 防火墙主动拒绝：防火墙策略是 REJECT（回 RST）而不是 DROP（静默丢）；
服务器崩溃了：listenfd 随进程消失，端口立刻释放，新连接被 RST。


关于 EINTR 的坑
connect 的 EINTR 处理和 accept / recv 不一样
// ❌ 错误：简单重试可能出问题
do {
    ret = connect(sockfd, &addr, sizeof(addr));
} while (ret < 0 && errno == EINTR);
POSIX 规定：connect 被信号打断后，连接状态未定义——
可能已经完成、可能还在进行、可能已经失败。简单重试会出问题
正确姿势
// 打断后，用 getsockopt(SO_ERROR) 检查连接状态
int ret = connect(sockfd, &addr, sizeof(addr));
if (ret < 0 && errno == EINTR) {
    int err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &len) == 0) {
        if (err == 0) {
            // 已经连上了
        } else {
            // 连接失败
        }
    }
}


阻塞 vs 非阻塞 connect
int sockfd = socket(AF_INET, SOCK_STREAM, 0);   // 阻塞
connect(sockfd, &addr, sizeof(addr));           // 阻塞到握手完成或失败.

阻塞 connect 的问题
服务器不响应时卡 127 秒  单线程卡死；   没有超时控制—
生产代码常用非阻塞 connect + 超时
/ ...较复杂代码... /


connect 成功并不代表"服务器 accept 了"！
客户端视角：connect 成功 = 连接建立（从协议层看是 ESTABLISHED
服务器视角：accept 成功 = 应用层"开始处理"。
两者中间有时间差——这就是 listen 章节里 backlog 的原因：连接堆在 accept
队列里等应用取走


连接已经建立，不能重复 connect 同一 socket。
要么用同一个 socket 继续通信，要么先 close 再 socket。

connect 时内核已经复制了地址信息，之后改用户态结构体不影响连接。
要连别的服务器必须换 socket。

send(sockfd, msg.data(), msg.size(), 0);
"hello tcp" 里没有 \n
服务器的 recv 收到 "hello tcp"，buf[n] = '\0' 后 buf 就是合法 C 字符串 "hello
tcp"（不含 \n）。 服务器 printf("[server] recv %zd bytes: %s\n", n, buf) 会输出
[server] recv 9 bytes: hello tcp

如果客户端加 \n（"hello tcp\n"，10 字节），服务器打印时会多一行
（\n 被打印，然后 printf 自己又加一个 \n）
[server] recv 10 bytes: hello tcp
                          ← 这里有个空行（因为 buf 里有 \n）

msg.data() vs msg.c_str()
send(sockfd, msg.data(), msg.size(), 0);
//          ↑ 和 c_str() 效果一样（C++11 后）
C++11 起 data() 和 c_str() 完全等价——都返回 const char*，都保证 \0 结尾。
send 只发 size() 个字节，\0 在不在范围内都不影响发送（因为 send 按长度发，不认
\0）。

msg.size() 而不是 strlen()
size() 是 O(1)（std::string 内部维护长度）；strlen() 是 O(n)（要扫描到 \0）。
而且如果 msg 里有 \0，strlen 会截断。
size() 是唯一正确写法

flags = 0 ——客户端的特殊考量
flags = 0
在客户端几乎总是没问题——因为客户端通常是主动方，不会先收到"对端关闭"的信号。
但理论上，如果服务器先 close，客户端再 send 也会触发 SIGPIPE。
稳健的客户端也带 MSG_NOSIGNAL

注意 EINTR 这里也没处理——和服务器端一样是 demo 简化。
生产代码要 do...while (errno == EINTR)


close(sockfd)
服务器端：close(connfd) 和 close(listenfd) 在 send 之后统一执行，所以 send
失败后继续往下走也能正常关闭—— 不泄漏，但语义不严谨（应该立即失败退出）
客户端：close(sockfd) 是唯一一次关闭，
如果 send 失败后不关，sockfd 就泄漏了——必须在错误分支里关。

客户端 sockfd 从头管到尾，每个错误分支都要关；
服务器 connfd 在步骤 6 和 7 之间统一关闭，错误分支可以"继续往下走"。
生产代码应该两边都严格——每个错误分支明确 close，不依赖"后面会关"

代码只处理了 sent < 0，没处理"部分发送"
"hello tcp" 9 字节，远小于内核发送缓冲区，一次必定发完——所以 demo 里不会暴露
和服务器端 send 一样——生产代码必须循环

printf("[client] sent: %s\n", msg.c_str());
为什么打印 msg 而不是 sent 字节数
printf("[client] sent: %s\n", msg.c_str());   // 客户端
printf("[server] sent %zd bytes\n", sent);    // 服务器
客户端打印"发送了什么内容"，服务器打印"发送了多少字节"。
两种都是合理的调试信息
取决于调试需求。生产里通常两者都打

send 返回成功只是"交给内核了"，数据可能还在网络里、可能还在内核缓冲区里。
要确认对端收到，必须等对端的 ACK 或应用层回复——这就是紧接着 recv 的作用

客户端 send 后如果不等 recv 就 close，服务器收到 FIN 时可能已经准备 send
了，触发 EPIPE / SIGPIPE。 服务器的 SIGPIPE 风险就是这么来的

recv()
维度	客户端	服务器
n == 0 的含义	服务器没回就关了（异常）	客户端发完关了（正常）
n == 0 的日志	"server closed before replying"	"peer closed"
return 0 还是 1	0（有争议，见后面）	0
错误时额外 close	只 close sockfd	close connfd + listenfd

客户端 n == 0 是异常场景（"我请求发出去了，你连回复都不给就挂"），
服务器 n == 0 是正常场景（"客户端聊完自己走了"）。

正常情况下客户端根本不该收到 0
正常情况下客户端 recv 拿到的是 "echo: hello tcp"，n > 0，然后自己关。
n == 0 意味着服务器在回复之前就关连接了
n == 0 是永久状态——对端已经关闭，recv 永远返回 0。必须 break / close / return。
n == 0 return 0 还是 return 1——这是个设计判断
如果客户端是"能连上就算成功"——连上了、发请求了、服务器挂了没回，但"整个流程走过一遍"，可以算"正常结束"；
如果客户端是"必须拿到回复才算成功"——那服务器没回就是失败，应该 return 1


客户端的阻塞模型——请求-响应模式
服务器视角——"被动等待"
服务器的 recv
没有"什么时候该收到数据"的预期——客户端可能立刻发，也可能永远不发（客户忘了点发送按钮）。
所以阻塞是合理的——服务器就是"随时待命"

客户端视角——"主动等待承诺的回复"
客户端的 recv 有明确的"预期时间"——我刚发出去请求，回复应该在 RTT +
服务器处理时间 内到。 如果等了 30 秒还没到，那大概率出问题了——这就是为什么客户端
recv 通常要设超时。 代码里没有超时——这是 demo 简化 生产代码必须设超时

代码只区分了 n == 0 和 n < 0，没细究 errno——demo 简化。
生产里 ECONNRESET 和 n == 0 要分开处理，因为：
n == 0：对端有序关闭，已收到的数据是完整的；
ECONNRESET：对端强制断开，可能有数据在缓冲区里没读出来。
*/
<-->


# tcp_server_multi.cpp

<!-->

/*
AI 解析：

为什么 setsockopt(SO_REUSEADDR) 可以不检查 < 0
返回值	含义
0	成功，选项已生效
-1	失败，errno 被设

核心判断标准：失败后程序还能不能用
类别	失败后果	处理策略	例子
关键调用	整个功能做不了	必须检查 < 0，失败就退出
socket、bind、listen、accept、connect、send、recv 非关键调用
部分功能降级，核心仍可用	可以只警告，或干脆不检查
setsockopt、getsockopt、close

失败的具体后果是"重启时可能要多等 60 秒"——不是"程序完全不能用"。
工程上最常见、最推荐的写法——"检查 + 警告 + 继续"。
*/

/*
文档资料：

htonl()函数
函数原型是：uint32_t htonl(uint32_t hostlong)
其中,hostlong是主机字节顺序表达的32位数，htonl中的h–host主机地址，to–to,n–net网络，l–unsigned
long无符号的长整型(32位的系统是4字节)； 函数返回值是一个32位的网络字节顺序；
函数的作用是将一个32位数从主机字节顺序转换成网络字节顺序。

htons()函数
函数原型是：uint16_t htons(uint16_t hostlong)
其中,hostlong是主机字节顺序表达的16位数，htons中的h–host主机地址，to–to,n–net网络，s–signed
long无符号的短整型(32位的系统是2字节)； 函数返回值是一个16位的网络字节顺序；
函数的作用是将一个16位数从主机字节顺序转换成网络字节顺序，简单的说就是把一个16位数高低位呼唤。

ntohs()函数
函数原型是：uint16_t ntohs(uint16_t hostlong)
其中,hostlong是网络字节顺序表达的16位数，ntohs中的,n–net网络，to–toh–host主机地址，s–signed
long有符号的短整型(32位的系统是2字节)； 函数返回值是一个16位的主机字节顺序；
函数的作用是将一个16位数由网络字节顺序转换为主机字节顺序，简单的说就是把一个16位数高低位互换。

ntohl()函数
函数原型是：uint32_t ntohs(uint32_t hostlong)
其中,hostlong是网络字节顺序表达的32位数，ntohs中的,n–net网络，to–toh–host主机地址，s–unsigned
long无符号的短整型(32位的系统是4字节)； 函数返回值是一个32位的主机字节顺序；
函数的作用是将一个32位数由网络字节顺序转换为主机字节顺序。
*/





/*
终端 1：起服务器
./tcp_server_multi
# [server] listening on 0.0.0.0:8080 ...

终端 2 / 终端 3：两个客户端同时连
./tcp_client_basic    # 终端 2
./tcp_client_basic    # 终端 3

服务器输出
[server] new client: 127.0.0.1:35490 (connfd=4)
[server] connfd=4 recv 9 bytes: Hello TCP
[server] connfd=4 sent 15 bytes
[server] connfd=4 peer closed

[server] new client: 127.0.0.1:35492 (connfd=5)
[server] connfd=5 recv 9 bytes: Hello TCP
[server] connfd=5 sent 15 bytes
[server] connfd=5 peer closed
*/

/*
AI 解析：

每个连接一个线程有什么问题？（连接数多了会怎样）

问题 1：线程是"昂贵资源"，默认就吃掉大量虚拟内存
Linux 上每个线程的默认栈大小是 8MB（ulimit -s 查看）
虽然虚拟内存不等于物理内存（按需分页），但：
内核为每个线程维护是真的占用物理内存；
很多个连接时，光线程管理开销就吃掉数百 MB 物理内存 + 大量内核对象

问题 2：线程数有硬上限
ulimit -u                 # 每用户进程/线程数上限
cat /proc/sys/kernel/threads-max   # 系统全局线程数上限
超过 ulimit -u：pthread_create 返回 EAGAIN（std::thread 构造抛 std::system_error）；
达到 threads-max 或内存不足：内核 OOM killer 直接杀进程

问题 3：上下文切换开销
内核调度器要不停切换线程；
每次切换要保存/恢复寄存器、切换内核栈、刷 TLB；
CPU 时间大量花在"切换"而不是"干活"上；
极端情况：加线程反而变慢（吞吐量下降）

问题 4：内存和锁竞争
共享数据需要加锁：如果多个线程要访问同一个全局结构
就必须加锁，而锁竞争又带来新的开销和死锁风险

问题 5：无法控制连接数
攻击者可以几秒钟开几千个连接，服务器瞬间创建几千个线程，直接 OOM。


detach() 版本和 join() 版本各有什么风险？

detach() 版本
① 无法知道线程什么时候结束，无法优雅关闭
// 想关闭服务器，只能：
// 1. Ctrl+C 杀进程 → 所有 detach 线程也被杀
// 2. 没有"等待所有连接处理完再退出"的能力
② 无法限制并发线程数
攻击者连 10 万次 → 服务器创建 10 万个线程 → OOM 被杀。这是 detach 版本最危险的地方。
③ 调试困难
崩溃时不知道是哪个线程（线程没有名字，日志里只有 connfd）；
无法追踪线程生命周期；
无法统计"当前有多少活着的线程"（除非自己维护 atomic 计数）
④ 主线程退出时行为不确定
main 返回后，detach 的线程可能被强制终止，也可能继续跑一会儿（取决于 libc 和内核）。
可能在 send 一半时被杀，客户端收到半个响应。

join() 版本
① vector 无限增长——每个线程对象不回收
std::thread 对象即使线程已结束，只要不 join，它还是 joinable 状态，占着资源。vector 只增不减：
内存增长
已结束的线程没有被 join，其内核资源没释放——等价于泄漏
长期运行的服务会越跑越多，最终撞上 threads-max 或 ulimit -u
② 无法在运行中 join——只有循环结束后才能 join
// 没有机会 join——因为不知道哪个线程完成了
for (auto& t : threads) t.join();   // 这行永远执行不到（accept 不退出）
③ 想"边跑边清理"需要额外机制
④ 优雅停机依然需要额外逻辑
这都远超 demo 演示范围

AI 提示：
真实生产的选择：两个都不选，用线程池（固定 N 个线程 + 任务队列）或 epoll 事件驱动——
前者限制资源，后者单线程处理上万连接


如果客户端发完消息不关闭连接，服务器会怎样？

现象：子线程永久阻塞，connfd 永不释放
while (true) {
    ssize_t n = recv(connfd, buf, sizeof(buf) - 1, 0);
    if (n < 0) { ... break; }
    if (n == 0) { ... break; }              // ← 只有对端关闭才会走到这里
    // 收到数据 → 回复 → 继续循环
}
close(connfd);                              // ← 只有 break 之后才执行

recv 在阻塞模式下会一直等——客户端不发数据、也不发 FIN，服务器就永远卡在 recv
线程永远不退出——永远占着资源
connfd 永不关闭——fd 泄漏
可能被利用此进行攻击

怎么解决(了解)
方案 1：设置 recv 超时
方案 2：应用层心跳 / 会话超时
方案 3：epoll + 定时器 / 事件驱动
方案 4：限制最大连接数
但这些都远超 demo 演示范围。
*/
<-->


# http_server.cpp

<!-->
/*
AI 解析文档：

static const std::string ROOT = ".";
定义"静态文件的根目录"——所有请求的 URL 路径都会拼在它后面，变成实际的文件路径
. —— 当前工作目录（cwd）
如果改成 ROOT = "/var/www/html"：
这就叫"静态文件根目录"

std::string —— 为什么不用 const char*
std::string 天然支持 + 拼接、+= 追加、.find() 查找等——char*
只是个指针，这些操作都没有。 既然要拼路径，用 std::string 是自然选择。

static 在这里不是"静态存储期"（std::string
本来就是全局生命周期），而是"内部链接（internal linkage）"—— 这个变量只在当前
.cpp 文件内可见，其他文件看不到

为什么不直接用绝对路径
因为绝对路径写死在代码里，换机器/换目录就跑不了：


ss << ifs.rdbuf(); out = ss.str();
把整个文件内容一次性"流式拷贝"到内存字符串里——ifs.rdbuf()
拿到文件流的底层缓冲区，
<< 把缓冲区全部内容搬到 ostringstream，str() 转成 std::string

① ifs.rdbuf() —— 拿到"底层的流缓冲区"
rdbuf() 返回 std::filebuf*——它代表"这个流背后真正的数据源"。
② ss << ifs.rdbuf() —— 重载的"流拷贝"
// 伪代码，<ostream> 里类似这样
// ostream& operator<<(ostream& os, std::streambuf* sb);
它的语义是："从 sb 这个缓冲区一直读到 EOF，把读到的每个字节都 os.put() 进去"。
③ ss.str() —— 把 ostringstream 的内容转成 std::string
std::ostringstream 内部维护一个动态增长的字符缓冲区。.str() 返回一份拷贝
注意：.str() 是值返回（拷贝），对于大文件可能有一次完整的内存拷贝。
C++20 有 .str() && 移动版本，但 demo 里是 C++11 兼容写法。

传统写法 1：seekg/tellg/read
//std::ifstream ifs(path, std::ios::binary);
//if (!ifs) return false;
//ifs.seekg(0, std::ios::end);       // 定位到文件末尾
//std::streamsize size = ifs.tellg(); // 拿到文件大小
//ifs.seekg(0, std::ios::beg);       // 回到开头
//out.resize(size);                  // 预分配
//ifs.read(&out[0], size);           // 一次读满

传统写法 2：循环 getline / read
//char buf[4096];
//while (ifs.read(buf, sizeof(buf))) {
//   out.append(buf, ifs.gcount());
//}
//out.append(buf, ifs.gcount());  // 处理最后不满一块的


make_response() 详解 —— HTTP 响应的"装配车间"
把"状态码 + 状态短语 + 头部 + body"按 HTTP/1.1
文本协议格式拼成一个完整字节串，返回给调用方去 send static std::string

make_response(int code, const std::string& status, const std::string&
content_type, const std::string& body); ① code 和 status 为什么分开传 HTTP
状态行是： HTTP/1.1 200 OK\r\n ↑   ↑ code  status（reason phrase） 理论上 code
和 status 是绑定的——200 就该是 OK，404 就该是 Not Found。 为什么不直接传
code，函数内部查表？

demo 里只用到 4 个状态码（200/400/403/404/405），写死查表反而更啰嗦：
static const std::map<int, std::string> REASONS = {
    {200, "OK"}, {400, "Bad Request"}, {403, "Forbidden"},
    {404, "Not Found"}, {405, "Method Not Allowed"},
};没必要为一个 demo 引 map 和查表。
HTTP 规范允许 reason phrase 任意——它只是给人看的，协议语义只看 code

② content_type 为什么传入而不是内部推导
理想情况：服务器应该根据文件后缀自动选 Content-Type：
但代码里是调用方传的——因为：
405/404 这类错误页面没有"文件后缀"，需要固定 text/plain
demo 把"文件类型推导"简化掉了——所有文件都当 text/html 返回

生产级实现会有个 mime_type(path) 函数，按后缀查表返回 MIME。demo 省略了。

③ body 传 const std::string& —— 零拷贝
HTML 文件可能几 MB，每次响应多拷贝一次是不必要的浪费。


为什么用 ostringstream 而不是 std::string +？
除了上面说过的
生产 HTTP 服务器会更底层，但 demo 用 ostringstream 是可读性和安全的平衡。

协议要求：
HTTP-version SP status-code SP reason-phrase CRLF
HTTP/1.1 —— 协议版本。必须大写
SP（单空格） —— 只能是一个空格
200 —— 状态码，3 位数字
OK —— reason phrase，可以是空字符串，但不推荐
\r\n —— CRLF，不是单独的 \n
HTTP/1.1 规范明文要求 CRLF（\r\n）

Windows	            CRLF                (\r\n)
Unix/Linux/macOS	LF (\n)         	简化为单字符换行
Mac OS          	CR (\r)	            早期苹果系统使用

ss << "Content-Type: " << content_type << "\r\n";
Content-Type	浏览器行为
text/html	当 HTML 解析渲染
text/plain	当纯文本显示（不解析标签）
image/png	当图片显示
application/json	不显示，交给 JS 处理
application/octet-stream	下载，不显示

代码里传的是：
"text/html; charset=utf-8"    // 200 响应
"text/plain"                   // 404/405 响应
charset=utf-8 至关重要——中文页面的命根子

text/html 和 text/html; charset=utf-8 的区别：
前者只有类型，编码靠浏览器猜（或 <meta>）；
后者明确编码，更可靠、更专业。

Content-Length
ss << "Content-Length: " << body.size() << "\r\n";  // ⚠ 必须是 body
的真实字节数 body.size() 是
std::string::size()——返回字节数（size_t），不是"字符数"。

场景	body.size()	Content-Length 应填	常见错误
ASCII 文本 "hello"	5	5	✅
中文 "你好"（UTF-8）	6	6	❌ 填 2（按字符数）
含 \r\n 的文件	实际字节	实际字节	❌ 在 Windows 上被文本模式转换
二进制文件（图片）	文件字节数	文件字节数	❌ 用 strlen() 遇到 \0
截断

Content-Length 填大了 → 客户端等不到足够数据 → 超时、连接卡死；
Content-Length 填小了 → 客户端只读前 N 字节 →
内容截断（表现为"页面显示一半"、"图片坏掉"） 完全没写 →
客户端靠连接关闭判断结束，浏览器看不到进度、无法判断完整性。 不要用
strlen(body.c_str())——遇到 \0 会截断。

ss << "Connection: close\r\n";
告诉客户端："这条连接只服务这一个请求，响应完就关"。
每个请求一次 TCP 握手。demo 场景下无所谓
生产情景
Connection: keep-alive    ← 声明复用（HTTP/1.1 默认）
Connection: Upgrade       ← 协议升级（如 WebSocket）


空行 —— 一个字节都不能少
ss << "\r\n";   // 空行：头部结束
这一个 \r\n 是 HTTP 响应的"命门"：
HTTP/1.1 200 OK\r\n           ← 状态行结束
Content-Type: text/html\r\n    ← 每个头结束
Content-Length: 15\r\n
Connection: close\r\n
\r\n                            ← 关键：空行 = 头部结束、body 开始
<h1>你好</h1>                   ← body

果少写这个 \r\n 会怎样：
客户端会认为 body 还没开始——它继续把 <h1>你好</h1> 解析成"头部的延续"；
头部里有非 Name:  解析失败 → 报错；
curl 报错：Malformed HTTP response；
浏览器：ERR_INVALID_HTTP_RESPONSE 或页面空白。

return ss.str();
把 ostringstream 的内部缓冲拷贝成 std::string 返回。
C++20 有移动版

为什么要"先构造再发"而不是边拼边发
Content-Length 要先算出来——如果不构造完整响应，Content-Length 和实际 body
分开两次写，容易不一致； 一次 send 系统调用 vs
五次——系统调用有开销，批量发减少上下文切换； 错误处理集中；  TCP 层面更友好


Date、Server 头
RFC 7231："服务器应该发送 Date
头，除非没有时钟"——"应该"不是"必须"，浏览器能处理缺失 Server 头：可选 超出 demo
范围，代码故意简化了。

生产级改进
Content-Type 应该由路径推导
Connection: close 应可配置
Content-Length 对"流式响应"的限制
超出 demo 范围


buf 和 req —— 两个完全不同的角色
std::string req;        // ① 累积容器（跨多次 recv）
char buf[1024];         // ② 单次接收缓冲（每次 recv 覆盖）

循环 recv
tcp_server_basic 是 demo——客户端发一条短消息，一次 recv
就能拿全（假设，可能出错但 demo 不暴露）； http_server
是真实协议——请求可能被拆，必须循环。

buf 的用法也不同：
之前：sizeof(buf) - 1（要留位给 buf[n] = '\0'）；
现在：sizeof(buf)（不用 \0，因为 req.append(buf, n) 按长度处理）。


req.append(buf, n) —— 为什么带长度 n
这是"按长度追加 n 字节"的重载：
std::string& append(const char* s, size_type n);
// 从 s 处拷贝 n 字节追加到末尾

为什么不用更简单的 req.append(buf)：
// ❌ 危险
req.append(buf);
append(const char* s) 这个重载依赖 \0 终止——它会一直读到 \0 才停。但 buf 没有 \0
终止符！

正确做法：显式告诉 append "只追加前 n 个字节"——req.append(buf, n)。

这也是为什么 recv 用 sizeof(buf) 而不是 sizeof(buf) - 1：
因为 append(buf, n) 不依赖 \0——buf 里是什么字节都无所谓，只要告诉 append 长度。
这和 tcp_server_basic 里的 recv(connfd, buf, sizeof(buf)-1, 0) 形成对比——
那里要 buf[n] = '\0' 当字符串用，所以必须留位。

当 C 字符串用	sizeof(buf) - 1	buf[n] = '\0'
按长度处理	sizeof(buf)	append(buf, n) 或 write(fd, buf, n)

HTTP 服务器是后者——因为它最终要把完整请求放进 std::string req，std::string
天然按长度管理，不依赖 \0。


std::string request_line = req.substr(0, line_end);
为什么"每次"都从 0
因为每个 handle_client 只处理一条连接、一个请求——req
每次都是新的，请求行永远在开头。 如果服务器支持
keep-alive（一条连接处理多个请求），那么第二个请求的请求行确实不在 req 的第 0
字节—— 那时候就需要维护一个"当前解析位置"的偏移量 demo 用 Connection:
close——一连接一请求，所以永远从 0 开始。

iss >> method >> path >> version 的规则
std::istringstream 的 operator>> 是格式化提取运算符。对 std::string
目标，它的规则是： ① 跳过前导空白 → ② 读取非空白字符直到下一个空白或流结束 → ③
把读到的内容给目标

空白（whitespace） 由 std::isspace() 定义，包括：
' '（空格）	✅
'\t'（Tab）	✅
'\n'（换行 LF）	✅
'\r'（回车 CR）	✅
'\v'（垂直 Tab）	✅
'\f'（换页）	✅
所以 \r 和 \n 都是空白——>> 遇到它们会像遇到空格一样停止读取。

method = "GET"
path = "/index.html"
version = "HTTP/1.1"

关键点：\r\n 不在 request_line 里
request_line = req.substr(0, line_end) 已经把 \r\n 切掉了：
所以 >> version 读的是 "HTTP/1.1"

如果不做 substr 会发生什么
req 是 "GET /index.html HTTP/1.1\r\nHost: ...\r\n\r\n"：
流里还有大量内容没读完：
所以 substr 是必须的——它把"请求行"和"请求头"分开，让 >>
只处理请求行那一小块干净的字符串

>> 遇到 \0 不认为是空白——会当成普通字符读入。
但 HTTP 请求行里有 \0 本身是非法输入，>> 之后解析出来的内容会带
\0，后面路由匹配会失败， 最终返回 404 或 400。

>> 和 getline 的差异
维度	iss >> token	std::getline(iss, line)
分隔符	任意空白（空格、Tab、\n、\r）	只按 \n 切
\r	当空白跳过	保留在字符串末尾
连续空白	自动跳过（多个当分隔）	保留
结果	一个 token	一整行（不含 \n，含 \r）
用途	解析"空格分隔的字段"（请求行）	解析"一行文本"（请求头每行）
// 要手动去掉 \r


路由 + 安全检查

第一段：if (path == "/") path = "/index.html";
浏览器/curl 访问 http://127.0.0.1:8080/ 时，请求行是：
GET / HTTP/1.1\r\n
    ↑
  path 就是 "/"
/ 本身不是一个文件——它是"网站根路径"。如果直接 ROOT + "/" = "./"，
std::ifstream("./") 打不开目录（ifstream 不能读目录），会返回 404。

用户输入 http://127.0.0.1:8080/ 看到 404，但输入
http://127.0.0.1:8080/index.html 正常

所以要把 / 映射到默认首页：
GET / HTTP/1.1        →  path = "/"  →  改成 "/index.html"  →  打开 ./index.html
GET /index.html       →  path = "/index.html"  →  不动  →  打开 ./index.html
这是 Web 服务器的通行惯例
用 curl -v / 和 curl -v /index.html 两次，结果完全一样——这就是这行代码的作用。

为什么是 /index.html 而不是别的
因为测试文件就叫 index.html——这是约定俗成的命名
生产服务器会按顺序试多个 demo 简化成"只看 index.html"

第二段：if (path.find("..") != std::string::npos)
攻击者发：
//GET /../../../etc/passwd HTTP/1.1
path = "/../../../etc/passwd"，filepath = ROOT + path：
//ROOT = "."
//filepath = "." + "/../../../etc/passwd" = "./../../../etc/passwd"

./../.. 的语义是"向上跳两级"——从当前目录往上走：
会成功打开 /etc/passwd——攻击者读到了系统用户列表。

path.find("..")     // 在 path 里找 ".." 这个子串
std::string::npos	没找到 .. → 安全
其他值（0、5 等）	找到了 .. 的下标 → 拒绝

拦不住的边缘情况（demo 里不管，生产要注意）
① URL 编码的 ..
攻击者可能用 %2e%2e 代替 ..：
② 绝对路径（path 以 / 开头）
③ 符号链接      ./link → /etc/passwd
④ Windows 的反斜杠      Windows 上 ..\ 也是上级目录：
demo 的威胁模型是"演示概念"。生产 HTTP 服务器必须做完整的路径规范化。
*/
<-->


# epoll_demo.cpp

<!-->

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
<-->

