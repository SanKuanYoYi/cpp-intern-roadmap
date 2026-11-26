#pragma once

#include "Channel.h"
#include "EventLoop.h"
#include "EventLoopThreadPool.h"
#include "TcpConnection.h"

#include <cstdint> //uint16_t port
#include <functional>
#include <map>
#include <memory>
#include <utility>

class EventLoop;

class TcpServer {
public:
  using MessageCallback = TcpConnection::MessageCallback;
  using ConnectionCallback = std::function<void(TcpConnection *)>;

  TcpServer(EventLoop *loop, uint16_t port, int numThreads = 4);
  ~TcpServer();

  TcpServer(const TcpServer &) = delete;
  TcpServer &operator=(const TcpServer &) = delete;

  void setMessageCallback(MessageCallback cb) {
    messageCallback_ = std::move(cb);
  }
  void setConnectionCallback(ConnectionCallback cb) {
    connectionCallback_ = std::move(cb);
  }

  void start();

private:
  void handleAccept();
  void removeConnection(TcpConnection *conn);

  EventLoop *loop_;
  int listenFd_ = -1;
  std::unique_ptr<Channel> acceptChannel_;
  std::unique_ptr<EventLoopThreadPool> threadPool_;     // 先声明
  std::map<int, std::unique_ptr<TcpConnection>> conns_; // 后声明

  MessageCallback messageCallback_;
  ConnectionCallback connectionCallback_;
};