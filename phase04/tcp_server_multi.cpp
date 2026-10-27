#include "net_common.h"
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

  int listenfd = create_listen_socket(8080);
  if (listenfd < 0)
    return -1;
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
