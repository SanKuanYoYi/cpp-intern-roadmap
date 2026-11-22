#pragma once

#include "Buffer.h"
#include "Channel.h"
#include "EventLoop.h"

#include <functional>
#include <string>
#include <utility>

class EventLoop;

class TcpConnection {
public:
  // 业务回调：拿到连接和它的读缓冲区，自己去切包
  using MessageCallback = std::function<void(TcpConnection *, Buffer &)>;
  // 关闭通知：上层据此把对象从 map 里删掉
  using ConnCloseCallback = std::function<void(TcpConnection *)>;

  TcpConnection(EventLoop *loop, int fd);
  ~TcpConnection();

  TcpConnection(const TcpConnection &) = delete;
  TcpConnection &operator=(const TcpConnection &) = delete;

  TcpConnection(TcpConnection &&) = delete;
  TcpConnection &operator=(TcpConnection &&) = delete;

  int fd() const { return channel_.fd(); }

  void setMessageCallback(MessageCallback cb) {
    messageCallback_ = std::move(cb);
  }
  void setConnCloseCallback(ConnCloseCallback cb) {
    ConnCloseCallback_ = std::move(cb);
  }

  void start();                       // 开始收数据
  void send(const std::string &data); // 发数据（内部处理写不完）
  void forceClose();                  // 立即关闭（错误路径用）

private:
  EventLoop *loop_;
  Channel channel_;
  Buffer in_;
  Buffer out_;
  bool closed_ = false;

  MessageCallback messageCallback_;
  ConnCloseCallback ConnCloseCallback_;

  void handleRead();  // EPOLLIN  → 读 → 交给 messageCallback_
  void handleWrite(); // EPOLLOUT → 补写
  void handleClose(); // FIN/对端挂 → 标记 + 通知
  void handleError(); // EPOLLERR → forceClose()
  void tryFlush();    // 把 out_ 尽量写完（handleWrite 和 send 共用）
};
