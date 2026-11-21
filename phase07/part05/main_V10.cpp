#include "Buffer.h"
#include "Channel.h"
#include "EpollPoller.h"
#include "EventLoop.h"
#include "TcpConnection.h"

#include "Buffer.cpp"
#include "Channel.cpp"
#include "EpollPoller.cpp"
#include "EventLoop.cpp"
#include "TcpConnection.cpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static void set_nonblocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags >= 0)
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// ===== 业务：唯一跟协议有关的地方 =====
static void process_frames(TcpConnection *c, Buffer &in) {
  while (in.readableBytes() >= 4) {
    uint32_t len = 0;
    std::memcpy(&len, in.peek(), 4);
    len = ntohl(len);

    if (len > 1024 * 1024) {
      c->forceClose();
      return;
    }

    if (in.readableBytes() < 4 + len)
      break;

    in.retrieve(4);
    std::string body(in.peek(), len);
    in.retrieve(len);

    uint32_t nlen = htonl(static_cast<uint32_t>(body.size()));
    std::string out(reinterpret_cast<const char *>(&nlen), 4);
    out += body;
    c->send(out);
  }
}

int main() {
  // 关键：listen_fd 必须非阻塞，否则下面的 accept 循环会卡死
  int listen_fd =
      socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
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

  EventLoop loop;
  std::map<int, std::unique_ptr<TcpConnection>> conns;

  // ① 轮末销毁：只剩三行
  loop.setCloseHandler([&](int fd) {
    auto it = conns.find(fd);
    if (it == conns.end())
      return;
    ::close(fd);
    conns.erase(it);
  });

  // ② accept
  auto on_accept = [&]() {
    while (true) {
      sockaddr_in client_addr{};
      socklen_t len = sizeof(client_addr);
      int fd = ::accept(listen_fd, (sockaddr *)&client_addr, &len);

      if (fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
          return;
        if (errno == EINTR)
          continue;
        return;
      }
      set_nonblocking(fd); // ⚠️ accept 出来的 fd 不继承 O_NONBLOCK

      auto conn = std::make_unique<TcpConnection>(&loop, fd);
      TcpConnection *raw = conn.get();

      raw->setMessageCallback(process_frames);
      raw->setCloseCallback(
          [&](TcpConnection *c) { loop.closeLater(c->fd()); });

      conns[fd] = std::move(conn); // 先入表
      raw->start();                // 再订阅

      char ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
      printf("Accepted connection from %s:%d\n", ip,
             ntohs(client_addr.sin_port));
    }
  };

  Channel listenCh(&loop, listen_fd);
  listenCh.setReadCallback(on_accept);
  listenCh.enableReading();

  loop.loop();
  return 0;
}

/*
python3 test_conn.py 1000

ps -o nlwp,rss,cmd -C server
# NLWP   RSS CMD
# 1  6480 ./server
*/