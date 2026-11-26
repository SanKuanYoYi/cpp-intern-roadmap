#include "TcpServer.h"
#include "Channel.h"
#include "EventLoop.h"
#include "EventLoopThreadPool.h"
#include "TcpConnection.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

static std::size_t tid() {
  return std::hash<std::thread::id>{}(std::this_thread::get_id());
}

TcpServer::TcpServer(EventLoop *loop, uint16_t port, int numThreads)
    : loop_(loop),
      listenFd_(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)),
      threadPool_(std::make_unique<EventLoopThreadPool>(loop, numThreads)) {
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

  threadPool_->start(); // ← 新增：先把 N 个 sub loop 起好

  acceptChannel_ = std::make_unique<Channel>(loop_, listenFd_);
  // Channel(loop, listenFd_) → events_ = 0，内核还不知道它存在
  acceptChannel_->setReadCallback([this] { handleAccept(); });
  acceptChannel_->enableReading();
}

void TcpServer::handleAccept() {
  while (true) {
    int fd =
        ::accept4(listenFd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);

    fprintf(stderr, "[accept] fd=%d tid=%zu\n", fd, tid());

    if (fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) // ET：读干净
        break;
      if (errno == EINTR)
        continue;
      fprintf(stderr, "[accept] error: %s\n", strerror(errno));
      break;
    }

    // --------- T14 的改动 ----------
    EventLoop *ioLoop = threadPool_->getNextLoop(); // ① 要一个 sub loop
    // 后面 std::move(conn) 之后 conn 就空了，必须提前拿裸指针
    auto conn =
        std::make_unique<TcpConnection>(ioLoop, fd); // ② 传 ioLoop，不是 loop_

    // ---------- 其余照旧 ----------
    TcpConnection *raw = conn.get();
    // ① 挂上"收到数据怎么办"（模板从 TcpServer 拷一份）
    raw->setMessageCallback(messageCallback_);
    // ② 挂上"连接关闭了怎么办"（登记删除）
    raw->setConnCloseCallback(
        [this](TcpConnection *c) { removeConnection(c); });

    conns_[fd] = std::move(conn); // ③ 先入库（T12 决策，别改）
    // 关键：start() 必须在 ioLoop 的线程里执行
    ioLoop->runInLoop([raw] { raw->start(); }); // ④ 启动必须回 io 线程
    if (connectionCallback_)
      connectionCallback_(raw); // ⑤ 最后通知
    // TcpConnection::start() 要判 closed_
  }
}

void TcpServer::removeConnection(TcpConnection *conn) {
  int fd = conn->fd(); // ① 拷贝成 int
  // conns_ 只在 base loop 线程增删：
  //   + 插入：handleAccept（base 线程）
  //   - 删除：此处排队回 base 线程
  // 因此 conns_ 不需要 mutex。
  loop_->queueInLoop([this, fd] { // ② 只捕获值 + this
    conns_.erase(fd);             // ③ 延迟到轮末执行
  });
}
