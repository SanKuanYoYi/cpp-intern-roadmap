#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

// ------------------------------------------------------------------
// 子线程：处理单个客户端连接
// 参数 connfd 用值传递（int 拷贝），安全；子线程只关心自己这条连接
// ------------------------------------------------------------------
void handle_client(int connfd) {
  char buf[1024];
  // 支持一个客户端连续发多条消息（循环 recv）
  while (true) {
    ssize_t n = recv(connfd, buf, sizeof(buf) - 1, 0);
    if (n < 0) {
      if (errno == EINTR) // 信号打断，重试
        continue;
      perror("recv");
      break;
    }
    if (n == 0) { // 对端正常关闭
      printf("[server] connfd=%d peer closed\n", connfd);
      break;
    }
    buf[n] = '\0';
    printf("[server] connfd=%d recv %zd bytes: %s\n", connfd, n, buf);

    // 回复 "echo: " + 收到的内容，循环发送防止部分发送
    std::string reply = std::string("echo: ") + buf;
    ssize_t total = 0;
    while (total < reply.size()) {
      ssize_t sent = send(connfd, reply.data() + total, reply.size() - total,
                          MSG_NOSIGNAL);
      if (sent < 0) {
        if (errno == EINTR)
          continue;
        perror("send");
        close(connfd);
        return;
      }
      if (sent == 0)
        break; // 防御性
      total += sent;
    }
    printf("[server] connfd=%d sent %zu bytes\n", connfd, total);
  }
  close(connfd); // 子线程负责关闭自己的连接
}

// ------------------------------------------------------------------
// 主线程：只负责 accept，把工作甩给子线程
// ------------------------------------------------------------------
int main() {

  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    perror("socket");
    return 1;
  }

  int opt = 1;
  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("getsockopt");
  }

  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port = htons(8080);

  if (bind(listenfd, reinterpret_cast<struct sockaddr *>(&servaddr),
           sizeof(servaddr)) < 0) {
    perror("bind");
    close(listenfd);
    return 1;
  }

  if (listen(listenfd, 128) < 0) {
    perror("listen");
    close(listenfd);
    return 1;
  }
  printf("[server] listening on 0.0.0.0:8080 ...\n");

  // ====== 方式 A：detach（最简单） ======
  // 每个连接一个线程，创建后立即分离，线程结束后资源自动回收
  while (true) {
    struct sockaddr_in cliaddr;
    socklen_t clilen = sizeof(cliaddr);
    int connfd;
    do {
      connfd = accept(listenfd, reinterpret_cast<struct sockaddr *>(&cliaddr),
                      &clilen);
    } while (connfd < 0 && errno == EINTR);

    if (connfd < 0) {
      perror("accept");
      break;
    }

    char ip[INET_ADDRSTRLEN] = {0};
    // 32位ipv4地址，用10进制+句点表示时，所使用内存长度的最大值
    inet_ntop(AF_INET, &cliaddr.sin_addr, ip, sizeof(ip));
    printf("[server] new client: %s:%d (connfd=%d)\n", ip,
           ntohs(cliaddr.sin_port), connfd);
    // ntohs 将一个16位数由网络字节顺序转换为主机字节顺序

    std::thread(handle_client, connfd).detach();
  }

  // ====== 方式 B：join（阻塞） ======
  /*
  // std::vector<std::thread> threads;
  // while (true) {
  //     ... accept 同上 ...
  //     threads.emplace_back(handle_client, connfd);
  //     // ⚠️ 关键问题：threads 只增不减
  // }
  // for (auto& t : threads) t.join();   // 实际上永远执行不到

  while(true) 永远退不出来，for (auto& t : threads) t.join() 是死代码。
  要真正跑起来，得加信号处理让主线程能跳出 accept 循环
  */

  close(listenfd);
  return 0;
}

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