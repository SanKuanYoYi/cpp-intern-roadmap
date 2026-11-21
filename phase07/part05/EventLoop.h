#pragma once

#include "EpollPoller.h"

#include <functional>
#include <utility>
#include <vector>

class Channel;

class EventLoop {
public:
  using CloseHandler = std::function<void(int fd)>;

  EventLoop();
  ~EventLoop();

  EventLoop(const EventLoop &) = delete;
  EventLoop &operator=(const EventLoop &) = delete;

  // 把“关闭 fd”这件事推迟到本轮事件处理完之后
  void closeLater(int fd) { pendingClose_.push_back(fd); }
  void setCloseHandler(CloseHandler h) { closeHandler_ = std::move(h); }

  void runOnce(); // 跑一轮：poll → 分发 → 处理延迟关闭
  void loop();    // 跑到天荒地老

  void updateChannel(Channel *ch) { poller_.update(ch); }
  void removeChannel(Channel *ch) { poller_.remove(ch); }

private:
  EpollPoller poller_;
  std::vector<Channel *> active_;
  std::vector<int> pendingClose_;
  CloseHandler closeHandler_;
};