#include "TcpServer.h"
#include "Channel.h"
#include "EventLoop.h"
#include "TcpConnection.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

TcpServer::TcpServer(EventLoop *loop, uint16_t port)
    : loop_(loop),
      listenFd_(
          socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) {
  if (listenFd_ < 0)
    throw std::runtime_error("socket()");

  int opt = 1;
  setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  if (bind(listenFd_, (sockaddr *)&addr, sizeof(addr)) < 0) {
    close(listenFd_);
    throw std::runtime_error("bind()");
  }

  if (listen(listenFd_, SOMAXCONN) < 0) {
    close(listenFd_);
    throw std::runtime_error("listen()");
  }
}

TcpServer::~TcpServer() {
  acceptChannel_.reset(); // 先让 Channel 死（可选，但干净）
  if (listenFd_ >= 0)
    close(listenFd_);
}

void TcpServer::start() {
  if (acceptChannel_)
    return; // ★ 幂等保护

  acceptChannel_ = std::make_unique<Channel>(loop_, listenFd_);
  // Channel(loop, listenFd_) → events_ = 0，内核还不知道它存在
  acceptChannel_->setReadCallback([this] { handleAccept(); });
  acceptChannel_->enableReading();
}

void TcpServer::handleAccept() {
  while (true) {
    int fd =
        ::accept4(listenFd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);

    if (fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      if (errno == EINTR)
        continue;
      break;
    }

    auto conn = std::make_unique<TcpConnection>(loop_, fd);
    // 后面 std::move(conn) 之后 conn 就空了，必须提前拿裸指针
    TcpConnection *raw = conn.get();

    // ① 挂上"收到数据怎么办"（模板从 TcpServer 拷一份）
    raw->setMessageCallback(messageCallback_);
    // ② 挂上"连接关闭了怎么办"（登记删除）
    raw->setCloseCallback([this](TcpConnection *c) { removeConnection(c); });
    // ③ 通知业务："新连接来了"（可选）
    if (connectionCallback_)
      connectionCallback_(raw);
    // ④ 先入表，再开闸
    conns_[fd] = std::move(conn);
    conn->start();
    // TcpConnection::start() 要判 closed_
  }
}

void TcpServer::removeConnection(TcpConnection *conn) {
  int fd = conn->fd();            // ① 拷贝成 int
  loop_->queueInLoop([this, fd] { // ② 只捕获值 + this
    conns_.erase(fd);             // ③ 延迟到轮末执行
  });
}
