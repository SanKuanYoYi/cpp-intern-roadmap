#include "net_common.h"

#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int create_listen_socket(int port) {
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    perror("socket");
    return -1;
  }

  // 允许重启时立即 bind（TIME_WAIT 不阻塞）
  int opt = 1;
  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt"); // 非致命，继续
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<uint32_t>(port));

  if (bind(listenfd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) <
      0) {
    perror("bind");
    close(listenfd);
    return -1;
  }

  if (listen(listenfd, 128) < 0) {
    perror("listen");
    close(listenfd);
    return -1;
  }

  return listenfd;
}

bool send_all(int fd, const char *data, size_t len) {
  size_t total = 0;
  while (total < len) {
    ssize_t n = send(fd, data + total, len - total, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      ;
      return false;
    }
    if (n == 0)
      return false;
    total += static_cast<size_t>(n);
  }
  return true;
}

ssize_t recv_safe(int fd, void *buf, size_t len) {
  while (true) {
    ssize_t n = recv(fd, buf, len, 0);
    if (n < 0 && errno == EINTR)
      continue;
    return n;
  }
}

/*
AI 解析：
几个"抽公共代码"的注意事项

① 不要抽"看起来一样但语义不同"的函数
recv_safe 抽的是"处理 EINTR 后读一次"
但如果某个程序要"循环读到 EAGAIN"（比如 ET 模式的 epoll）——那不是 recv_safe

② send_all 里没处理 EAGAIN
send_all 遇到 EAGAIN 直接 return false——这在阻塞 fd 下永远不会发生——但在非阻塞
fd 下可能会。 如果 epoll_demo 要用 send_all——它需要 send_all 返回"发了多少 +
是否部分"——不能用这个版本。

③ create_listen_socket 只做"基础配置"
SO_REUSEADDR、listen(fd, 128) 是"几乎所有 TCP 服务器都要的"
但
SO_REUSEPORT（多进程共享端口）、TCP_NODELAY（低延迟）——这些是"特定场景才要"——不放进公共函数——
调用方需要时自己 setsockopt

④ net_common 不加 static
net_common 里的函数是"公开 API"，不能 static。
*/