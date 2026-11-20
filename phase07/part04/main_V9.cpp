#include "Buffer.h"
#include "Channel.h"

#include "Buffer.cpp"
#include "Channel.cpp"
#include "EventLoop.h"

#include <arpa/inet.h> // inet_ntop
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio> // printf, perror
#include <map>
#include <memory>
#include <netinet/in.h> // sockaddr_in
#include <pthread.h>    // pthread
#include <sys/epoll.h>  // epoll
#include <sys/socket.h> // socket
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h> // read, write, close

struct Conn {
  Conn(EventLoop *loop, int f) : ch(loop, f) {}
  Buffer in;  // 收：内核 → 用户态，可能只有半个帧
  Buffer out; // 发：待发送队列
  Channel ch; // ← 内嵌 Channel
};

static const int kMaxEvents = 1024;

static const size_t kMaxFrame = 10 * 1024 * 1024; // 10MB

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

  EventLoop loop;
  std::map<int, std::unique_ptr<Conn>> conns; // ← 存 unique_ptr

  // 前置声明，因为回调用到
  std::function<void()> on_accept;
  std::function<void(Conn *)> on_read, on_write;
  std::function<void(int)> close_conn;
  std::function<bool(Conn *)> process_frames;

  // 监听 Channel
  Channel listenCh(&loop, listen_fd);
  listenCh.setReadCallback(on_accept);
  listenCh.enableReading();

  // ---------- 关闭：只干真活，不做“该不该关”的判断 ----------
  close_conn = [&](int fd) {
    // ⑥ close_conn 里（close_conn 开头）
    fprintf(stderr, "[close] fd=%d\n", fd);

    auto it = conns.find(fd);
    if (it == conns.end()) // 已经关过了，幂等
      return;

    loop.removeChannel(&it->second->ch); // ① 先从 epoll 摘掉
    close(fd);                           // ② 再关 fd
    conns.erase(it);                     // ③ 最后销毁对象
  };
  loop.setCloseHandler(close_conn);

  // ---------- 写干净 ----------
  auto try_flush = [&](Conn *c) {
    while (c->out.readableBytes() > 0) {
      ssize_t n = ::write(c->ch.fd(), c->out.peek(), c->out.readableBytes());
      if (n > 0) {
        c->out.retrieve(static_cast<size_t>(n));
        continue;
      }
      if (n < 0 && errno == EINTR)
        continue;
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        if (!c->ch.isWriting())
          c->ch.enableWriting();
        return;
      }
      loop.closeLater(c->ch.fd()); // ← 改动 2
      return;
    }
    if (c->ch.isWriting())
      c->ch.disableWriting();
  };

  on_accept = [&] {
    while (true) {
      // 给一个结构体来接收对端的 IP + 端口
      sockaddr_in client_addr{};
      socklen_t len = sizeof(client_addr);
      int conn = accept4(listen_fd, (sockaddr *)&client_addr, &len,
                         SOCK_CLOEXEC | SOCK_NONBLOCK);
      // accept4 = accept + 顺手设置 fd(flags) 标志，少一次系统调用
      // 没有 accept4 的话，需要多调用 fcntl
      if (conn < 0)
        break; // 简化

      auto c = std::make_unique<Conn>(&loop, conn);
      Conn *raw = c.get(); // ← 拿裸指针

      raw->ch.setReadCallback([&, raw] { on_read(raw); });
      raw->ch.setWriteCallback([&, raw] { on_write(raw); });
      raw->ch.setCloseCallback([&, raw] { loop.closeLater(raw->ch.fd()); });
      raw->ch.setErrorCallback([&, raw] { loop.closeLater(raw->ch.fd()); });
      raw->ch.enableReading();
      conns[conn] = std::move(c); // ← unique_ptr 进 map，所有权转移
    }
  };

  on_read = [&](Conn *c) {
    while (true) {
      int err = 0;
      ssize_t r = c->in.readFd(c->ch.fd(), &err);
      if (r > 0) {
        if (!process_frames(c)) {
          loop.closeLater(c->ch.fd());
          return;
        }
        try_flush(c);
        continue;
      }
      if (r == 0) {
        loop.closeLater(c->ch.fd());
        return;
      }
      if (err == EAGAIN || err == EWOULDBLOCK)
        break;
      if (err == EINTR)
        continue;
      loop.closeLater(c->ch.fd());
      return;
    }
  };

  on_write = [&](Conn *c) { try_flush(c); };

  process_frames = [&](Conn *c) {
    while (true) {
      // ① 头都没凑齐（连 4 字节都没有）→ 回去等更多数据
      if (c->in.readableBytes() < 4)
        return true;

      // 读 4 字节大端长度
      const unsigned char *p =
          reinterpret_cast<const unsigned char *>(c->in.peek());
      uint32_t len = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                     (uint32_t(p[2]) << 8) | uint32_t(p[3]);

      // ② 超长帧 → 协议错误，必须断开
      if (len > kMaxFrame)
        return false;

      // ③ 头齐了，但 body 没凑齐 → 回去等
      if (c->in.readableBytes() < 4 + static_cast<size_t>(len))
        return true;

      // ④ 一个完整帧到手
      c->in.retrieve(4); // 吃掉 4 字节头

      char hdr[4] = {// 回写同样的 4 字节头
                     char(len >> 24 & 0xff), char((len >> 16) & 0xff),
                     char((len >> 8) & 0xff), char(len & 0xff)};
      //& 0xff 是按位与操作，0xff 就是十六进制的 255，二进制是 11111111（8 个
      // 1）。作用是只保留低 8 位，把高位全部清零，也就是"取一个字节"。
      c->out.append(hdr, 4);                                 // 先写头
      c->out.append(c->in.peek(), static_cast<size_t>(len)); // 再写 body
      c->in.retrieve(static_cast<size_t>(len));              // 吃掉 body

      // ⑤ while 回到顶部，继续看 in 里还有没有下一个完整帧
    }
  };
}
