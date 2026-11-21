#include "Buffer.h"
#include "Channel.h"
#include "EventLoop.h"
#include "TcpConnection.h"

#include "Buffer.cpp"
#include "Channel.cpp"
#include "EventLoop.cpp"
#include "TcpConnection.cpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <memory>
#include <netinet/in.h>
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

    c->send(body);
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
      int fd = ::accept(listen_fd, nullptr, nullptr);

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
    }
  };

  Channel listenCh(&loop, listen_fd);
  listenCh.setReadCallback(on_accept);
  listenCh.enableReading();

  loop.loop();
  return 0;
}