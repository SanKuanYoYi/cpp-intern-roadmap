#pragma once

#include "EventLoop.h"

#include <condition_variable>
#include <mutex>
#include <thread>

class EventLoop;

class EventLoopThread {
public:
  EventLoopThread();
  ~EventLoopThread();

  EventLoopThread(const EventLoopThread &) = delete;
  EventLoopThread &operator=(const EventLoopThread &) = delete;

  // 启动线程，返回其内部 EventLoop*（永远非空）
  EventLoop *startLoop();

private:
  void threadFunc();

  EventLoop *loop_ = nullptr;    // 指向子线程栈上的 EventLoop（join 前有效）
  std::thread thread_;           // 子线程句柄
  std::mutex mutex_;             // 保护 loop_
  std::condition_variable cond_; // startLoop 等 loop_ 就绪
};