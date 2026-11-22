#include "TcpConnection.h"
#include "Channel.h"
#include <cerrno>
#include <cstddef>
#include <sys/types.h>
#include <unistd.h>

TcpConnection::TcpConnection(EventLoop *loop, int fd)
    : loop_(loop), channel_(loop, fd), in_(), out_(), closed_(false) {
  channel_.setReadCallback([this] { handleRead(); });
  channel_.setWriteCallback([this] { handleWrite(); });
  channel_.setCloseCallback([this] { handleClose(); });
  channel_.setErrorCallback([this] { handleError(); });
} // ★ 顺序必须跟声明顺序一致
TcpConnection::~TcpConnection() { loop_->removeChannel(&channel_); }

void TcpConnection::start() { channel_.enableReading(); }

void TcpConnection::send(const std::string &data) {
  if (closed_)
    return; // ⚠️ 必须加
  out_.append(data);
  tryFlush();
}

void TcpConnection::tryFlush() {
  while (out_.readableBytes() > 0) {
    ssize_t n = write(channel_.fd(), out_.peek(), out_.readableBytes());

    if (n > 0) {
      out_.retrieve(static_cast<size_t>(n));
      continue;
    }

    if (n == 0) { // count>0 却写 0，防御
      if (!channel_.isWriting())
        channel_.enableWriting();
      return;
    }

    if (n < 0 && errno == EINTR)
      continue;
    ;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (!channel_.isWriting())
        channel_.enableWriting(); // 订阅 EPOLLOUT
      return;
    }

    forceClose();
    return;
  }

  if (channel_.isWriting())
    channel_.disableWriting(); // 写干净了，退订
}

void TcpConnection::handleRead() {
  while (true) {
    int savedErrno = 0;
    ssize_t n = in_.readFd(channel_.fd(), &savedErrno);

    if (n > 0) {
      if (messageCallback_)
        messageCallback_(this, in_); // 交给业务切包
      if (closed_)
        return; // ⚠️ 关键
      continue; // ET：继续读
    }

    if (n == 0) {
      handleClose();
      return;
    } // FIN

    if (n < 0 && savedErrno == EINTR)
      continue;
    if (n < 0 && (savedErrno == EAGAIN || savedErrno == EWOULDBLOCK))
      break; // 读干净了

    handleClose();
    return;
  }
}

void TcpConnection::handleWrite() {
  if (closed_)
    return;
  tryFlush();
}

void TcpConnection::handleClose() {
  if (closed_)
    return;              // ① 防重复：同一轮可能被调 2~3 次
  closed_ = true;        // ② 标记：从此刻起 send() 无效
  channel_.disableAll(); // ③ 退订：让 epoll 立刻忘掉我（可选但推荐）
  if (ConnCloseCallback_)
    ConnCloseCallback_(this); // ④ 只通知，不自杀
}

void TcpConnection::handleError() { forceClose(); }

void TcpConnection::forceClose() { handleClose(); }
