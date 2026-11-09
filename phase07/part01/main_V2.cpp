#include <arpa/inet.h> // inet_ntop
#include <cerrno>
#include <cstdio>       // printf, perror
#include <netinet/in.h> // sockaddr_in
#include <pthread.h>    // pthread
#include <sys/epoll.h>  // epoll
#include <sys/socket.h> // socket
#include <sys/types.h>
#include <unistd.h> // read, write, close
#include <unordered_map>

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
  ev.events = EPOLLIN;
  ev.data.fd = listen_fd;
  epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

  const int kMaxEvents = 1024;
  epoll_event events[kMaxEvents];
  std::unordered_map<int, char> conns; // 只记"这个 fd 是连接"，char 只是占位
  // conns 是用户态自己的账本

  printf("Server is listening on port 8080...\n");

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
            break;         // 简化
          conns[conn] = 0; // accept 时：登记新连接
          epoll_event cev{};
          cev.events = EPOLLIN;
          cev.data.fd = conn;
          epoll_ctl(epfd, EPOLL_CTL_ADD, conn, &cev);

          char ip[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
          printf("Accepted connection from %s:%d\n", ip,
                 ntohs(client_addr.sin_port));
        }
        continue;
      }

      // 是已有连接 → 走 read/echo 分支
      char buf[4096];
      ssize_t r = read(fd, buf, sizeof(buf));
      if (r > 0) {
        write(fd, buf, r);
      } else {
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        conns.erase(fd); // 关闭时：从表里删掉
      }
    }
  }
  close(epfd);
  close(listen_fd);
}

/*
终端 B
python3 test/conn.py 100

终端 C（60 秒内执行）
ps -o pid,nlwp,vsz,rss,cmd -C server

# PID NLWP    VSZ   RSS CMD
# 7300    1   6896  3960 ./server

# PID NLWP    VSZ   RSS CMD
# 7300    1   6896  4104 ./server

# PID NLWP    VSZ   RSS CMD
# 7300    1   6896  4124 ./server

*/