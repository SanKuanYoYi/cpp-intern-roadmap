/*
AI 先行解析提示：

服务器端就像总机，先准备好“电话线”并“监听”来电；客户端则主动“拨号”。

客户端流程：
socket() → connect() → send() → recv() → close()
socket()：创建通信端点。
connect()：向服务器的IP和端口发起连接请求（拨号）。
send() / recv()：连接成功后，发送和接收数据。
close()：关闭连接。
*/

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

int main() {
  // ---------- 1. socket() ----------
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket");
    return 1;
  }

  // ---------- 2. connect() ----------
  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(8080);

  // 点分十进制字符串 → 32 位网络字节序整数
  if (inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr) != 1) {
    fprintf(stderr, "inet_pton: invalid address\n");
    close(sockfd);
    return -1;
  }

  // connect 会触发三次握手
  if (connect(sockfd, reinterpret_cast<struct sockaddr *>(&servaddr),
              sizeof(servaddr)) < 0) {
    perror("connect");
    close(sockfd);
    return -1;
  }
  printf("[client] connected to 127.0.0.1:8080\n");

  

  return 0;
}

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


*/