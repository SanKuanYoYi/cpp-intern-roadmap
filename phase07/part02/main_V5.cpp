#include "Buffer.cpp"
#include "Buffer.h"

#include <arpa/inet.h> // inet_ntop
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>       // printf, perror
#include <netinet/in.h> // sockaddr_in
#include <pthread.h>    // pthread
#include <sys/epoll.h>  // epoll
#include <sys/socket.h> // socket
#include <sys/types.h>
#include <unistd.h> // read, write, close
#include <unordered_map>
#include <vector>

struct Conn {
  int fd = -1;
  Buffer out; // 替代 std::string out + size_t sent
  bool want_write = false;
};

static const int kMaxEvents = 1024;

static void mod_events(int epfd, int fd, bool want_write) {
  epoll_event ev{};
  ev.events = EPOLLIN | EPOLLET | (want_write ? EPOLLOUT : 0);
  ev.data.fd = fd;
  epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

int main() {
  // 关键：listen_fd 必须非阻塞，否则下面的 accept 循环会卡死
  int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (listen_fd < 0) {
    perror("socket");
    return -1;
  }

  int opt = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(8080);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(listen_fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    return -1;
  }

  if (listen(listen_fd, 128) < 0) {
    perror("listen");
    return -1;
  }

  int epfd = epoll_create1(EPOLL_CLOEXEC);
  // 在 CLOEXEC 模式下，当一个进程调用 fork () 创建子进程或调用 exec ()
  // 执行一个新程序时，内核会自动关闭 epoll 实例，以防止在子进程或新程序中泄漏
  // epoll 文件描述符
  epoll_event ev{};
  ev.events = EPOLLIN | EPOLLET;
  ev.data.fd = listen_fd;
  epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

  epoll_event events[kMaxEvents];
  std::unordered_map<int, Conn> conns;
  std::vector<int> to_close; // 延迟关闭，避免边遍历边删

  auto close_conn = [&](int fd) { to_close.push_back(fd); };

  auto flush = [&](Conn &c) {
    while (c.out.readableBytes() > 0) {
      ssize_t n = write(c.fd, c.out.peek(), c.out.readableBytes());
      if (n > 0) {
        c.out.retrieve(static_cast<size_t>(n));
      } else if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          if (!c.want_write) {
            c.want_write = true;
            mod_events(epfd, c.fd, true);
          }
          return;
        }
        if (errno == EINTR)
          continue;
        close_conn(c.fd);
        return;
      } else {
        close_conn(c.fd); // n == 0，当作异常处理
        return;
      }
    }

    if (c.want_write) {
      c.want_write = false;
      mod_events(epfd, c.fd, false);
    }
  };

  while (true) {
    int n = epoll_wait(epfd, events, kMaxEvents, -1); // 就绪个数
    // 每个 fd 会 wait 返回多次
    if (n < 0) {
      if (errno == EINTR)
        continue;
      perror("epoll_wait");
      break;
    }

    for (int i = 0; i < n; i++) {
      int fd = events[i].data.fd;
      uint32_t evs = events[i].events;

      // 判断：这个就绪的 fd 是"监听 socket"还是"客户端连接 socket"
      // 因为 listen_fd 和所有客户端 conn 都注册在同一个 epfd 里
      // epoll_wait 返回的 events[i].data.fd 可能是任意一个
      if (fd == listen_fd) {
        // 是新连接 → 走 accept 分支
        // “等于”代表的是监听套接字发生了事件(餐厅门铃)
        // “不等于”代表的是某个已连接客户端发生了事件(餐厅内桌上客人)
        while (true) {
          // TCP 连接请求在 listen() 的 backlog 队列里排队
          // 这依赖 listen_fd 是非阻塞的（代码第 1 行 SOCK_NONBLOCK）。
          //  如果是阻塞的，第二次 accept 在没连接时会永久阻塞，整个单线程卡死

          // 给一个结构体来接收对端的 IP + 端口
          sockaddr_in client_addr{};
          socklen_t len = sizeof(client_addr);
          int conn = accept4(listen_fd, (sockaddr *)&client_addr, &len,
                             SOCK_CLOEXEC | SOCK_NONBLOCK);
          // accept4 = accept + 顺手设置 fd(flags) 标志，少一次系统调用
          // 没有 accept4 的话，需要多调用 fcntl
          if (conn < 0)
            break; // 简化
          conns.emplace(conn, Conn{conn});

          epoll_event cev{};
          cev.events = EPOLLIN | EPOLLET;
          cev.data.fd = conn;
          epoll_ctl(epfd, EPOLL_CTL_ADD, conn, &cev);

          char ip[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
          printf("Accepted connection from %s:%d\n", ip,
                 ntohs(client_addr.sin_port));
        }
        continue;
      }

      auto it = conns.find(fd);
      if (it == conns.end())
        continue;
      Conn &c = it->second;

      if (evs & (EPOLLERR | EPOLLHUP)) {
        // 这两个标志不需要你注册也会触发（epoll
        // 特殊语义），代表"连接坏了/对端彻底走了"。
        // 优先判断它们、立刻标记关闭、continue 跳过后续 read/flush
        close_conn(fd);
        continue;
      }

      bool alive = true;
      if (evs & EPOLLIN) {

        while (alive) {
          int err = 0; // ← 每次循环内重新初始化
          ssize_t r = c.out.readFd(fd, &err);
          if (r > 0) {
            continue; // 读到了，继续读
          } else if (r == 0) {
            close_conn(fd);
            alive = false; // FIN
          } else {
            if (err == EAGAIN || err == EWOULDBLOCK)
              break;
            if (err == EINTR)
              continue;
            close_conn(fd);
            alive = false;
          }
        }
      }

      if (alive && c.out.readableBytes())
        flush(c); // 有东西就尝试发
    }

    for (int fd : to_close) {
      epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
      close(fd);
      conns.erase(fd);
    }
    to_close.clear(); // clear() 保证每一轮只处理本轮新登记的要关
                      // fd，不重复关闭上一轮的。
  }
}

/*
终端 B：
python3 test/big_send.py
# echoed: 1048576
*/

/*
终端 B：
python3 test/slow_client.py
终端 C：
nc 127.0.0.1 8080
# (立刻回显)
*/

/*
终端 B：
python3 test/test_conn.py 1000

终端 C：
ps -o nlwp,rss,cmd -C server
# NLWP   RSS CMD
# 1  5136 ./server
*/