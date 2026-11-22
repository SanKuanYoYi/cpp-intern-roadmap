#pragma once

#include "EpollPoller.h"

#include <functional>
#include <utility>
#include <vector>

class Channel;

class EventLoop {
public:
  using Functor = std::function<void()>;

  EventLoop();
  ~EventLoop();

  EventLoop(const EventLoop &) = delete;
  EventLoop &operator=(const EventLoop &) = delete;

  // 延迟处理
  void queueInLoop(Functor fn) { pendingFunctors_.push_back(std::move(fn)); }

  void runOnce(); // 跑一轮：poll → 分发 → 处理延迟关闭
  void loop();    // 跑到天荒地老

  void updateChannel(Channel *ch) { poller_.update(ch); }
  void removeChannel(Channel *ch) { poller_.remove(ch); }
  // EventLoop::removeChannel 在 T12 变成了死代码

private:
  void doPendingFunctors();

  EpollPoller poller_;
  std::vector<Channel *> active_;
  std::vector<Functor> pendingFunctors_;
};