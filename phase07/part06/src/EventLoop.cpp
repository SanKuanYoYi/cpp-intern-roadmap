#include "EventLoop.h"
#include "Channel.h"
#include <vector>

EventLoop::EventLoop() { active_.reserve(64); }
EventLoop::~EventLoop() = default;

void EventLoop::runOnce() {
  active_.clear();
  poller_.poll(-1, active_);

  for (Channel *ch : active_) {
    ch->handleEvent();
  }

  // 本轮所有回调都返回了，才执行延迟动作
  doPendingFunctors();
}

void EventLoop::loop() {
  for (;;)
    runOnce();
}

void EventLoop::doPendingFunctors() {
  std::vector<Functor> tmp;
  tmp.swap(pendingFunctors_); // 关键：先换出来，再执行
  for (auto &f : tmp)
    f(); // 中途新的任务会到已置空的 pendingFunctors 中给下一轮
}
