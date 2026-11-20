#include "EventLoop.h"
#include "Channel.h"

EventLoop::EventLoop() { active_.reserve(64); }
EventLoop::~EventLoop() = default;

void EventLoop::runOnce() {
  active_.clear();
  poller_.poll(-1, active_);

  for (Channel *ch : active_) {
    ch->handleEvent();
  }

  // 本轮所有回调都跑完了，现在才真正销毁对象
  for (int fd : pendingClose_) {
    if (closeHandler_)
      closeHandler_(fd);
  }
  pendingClose_.clear();
}

void EventLoop::loop() {
  for (;;)
    runOnce();
}