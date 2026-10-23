#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

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

  // ---------- 3. send() ----------
  const std::string msg = "Hello TCP";
  ssize_t sent = send(sockfd, msg.data(), msg.size(), 0);
  if (sent < 0) {
    perror("send");
    close(sockfd);
    return -1;
  }
  printf("[client] sent: %s\n", msg.c_str());

  // ---------- 4. recv() ----------
  char buf[1024];
  ssize_t n = recv(sockfd, buf, sizeof(buf) - 1, 0);
  if (n < 0) {
    perror("recv");
    close(sockfd);
    return -1;
  }
  if (n == 0) {
    printf("[client] server closed before replying\n");
    close(sockfd);
    return 0;
    // return -1;
  }
  buf[n] = '\0';
  printf("[client] reply: %s\n", buf);

  // ---------- 5. close() ----------
  close(sockfd);
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


/*
# 终端 1：起服务器
./tcp_server_basic

# 终端 2：确认监听（新系统 netstat 已被 ss 取代，两个都能用）
netstat -tunlp | grep 8080
# 输出形如：tcp  0  0 0.0.0.0:8080  0.0.0.0:*  LISTEN  12345/./tcp_server_basic
ss -tlnp | grep 8080        # 等价命令

# 终端 3：发消息
./tcp_client_basic
# 客户端打印: reply: echo: hello tcp
*/