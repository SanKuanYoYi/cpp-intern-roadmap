#pragma once

#include "EventLoop.h"
#include "EventLoopThread.h"

#include <cstddef>
#include <memory>
#include <vector>

class EventLoop;

class EventLoopThreadPool {
public:
  EventLoopThreadPool(EventLoop *baseLoop, int numThreads);
  ~EventLoopThreadPool();

  void start();
  EventLoop *getNextLoop();

private:
  EventLoop *baseLoop_;
  int numThreads_;
  bool started_ = false;
  size_t next_ = 0;

  std::vector<std::unique_ptr<EventLoopThread>> threads_;
  std::vector<EventLoop *> loops_;
};