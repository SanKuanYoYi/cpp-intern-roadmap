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