#include "EventLoop.h"
#include "Channel.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

// ============ 构造：装门铃 ============
EventLoop::EventLoop() : threadId_(std::this_thread::get_id()) {
  wakeupFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (wakeupFd_ < 0) {
    throw std::runtime_error("eventfd failed");
  }

  wakeupChannel_ = std::make_unique<Channel>(this, wakeupFd_);
  wakeupChannel_->setReadCallback([this] { handleWakeup(); });
  wakeupChannel_->enableReading(); // → poller_.update()，注册 EPOLLIN
}

// ============ 析构：摘铃 → 关 fd → 成员逆序析构 ============
EventLoop::~EventLoop() {
  wakeupChannel_->disableAll(); // 从 epoll 摘掉（poller_ 还活着）
  close(wakeupFd_);             // 门铃 fd 的所有者是我，我关
}

// ============ 主循环 ============
void EventLoop::loop() {
  while (!quit_) {
    runOnce();
  }
}

void EventLoop::quit() {
  quit_ = true;
  if (!isInLoopThread()) {
    wakeup(); // 否则 loop 卡在 epoll_wait(-1) 里出不来
  }
}

void EventLoop::runOnce() {
  active_.clear();
  poller_.poll(-1, active_); // ← 门铃一响，这里就返回
  for (Channel *ch : active_)
    ch->handleEvent(); // 其中一个 ch 是 wakeupChannel_
  doPendingFunctors(); // 整轮事件处理完才拆箱子
}

// ============ 投递 ============
void EventLoop::runInLoop(Functor fn) {
  if (isInLoopThread())
    fn(); // 同步：返回时已执行完
  else
    queueInLoop(std::move(fn)); // 异步：排队 + 按铃
}

void EventLoop::queueInLoop(Functor fn) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingFunctors_.push_back(std::move(fn));
  } // ← 锁在这里就放掉，别包住 wakeup
  if (!isInLoopThread() || callingPendingFunctors_) {
    wakeup();
  }
}

void EventLoop::doPendingFunctors() {
  std::vector<Functor> tmp;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    tmp.swap(pendingFunctors_); // ① 锁内 swap，O(1)
  }

  // ② 锁外执行
  callingPendingFunctors_ = true;
  for (auto &f : tmp)
    f(); // 锁外执行（防死锁）
  callingPendingFunctors_ = false;
}

// ============ 门铃 ============
void EventLoop::wakeup() {
  uint64_t one = 1;
  ssize_t n = write(wakeupFd_, &one, sizeof one);
  static_cast<void>(n);
}

void EventLoop::handleWakeup() {
  uint64_t value;
  ssize_t n = read(wakeupFd_, &value, sizeof value);
  static_cast<void>(n);
}
