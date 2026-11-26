#include "EventLoopThreadPool.h"
#include "EventLoop.h"
#include "EventLoopThread.h"

#include <cstddef>
#include <memory>

EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop, int numThreads)
    : baseLoop_(baseLoop), numThreads_(numThreads) {}

EventLoopThreadPool::~EventLoopThreadPool() = default;

void EventLoopThreadPool::start() {
  if (started_)
    return; // ← 幂等

  started_ = true;

  if (numThreads_ <= 0) { // ① 退化：不另开线程
    loops_.push_back(baseLoop_);
    return;
  }

  loops_.reserve(static_cast<std::size_t>(numThreads_));
  threads_.reserve(static_cast<std::size_t>(numThreads_));

  for (int i = 0; i < numThreads_; i++) {
    auto t = std::make_unique<EventLoopThread>();
    loops_.push_back(t->startLoop()); // ② 阻塞到该线程就绪
    threads_.push_back(std::move(t)); // ③ 转交所有权
  }
}

// 只在 base loop 线程调用（唯一调用点：TcpServer::handleAccept）
// 因此 next_ 不需要加锁——这是「单写者」保证，不是天生线程安全。
EventLoop *EventLoopThreadPool::getNextLoop() {
  if (loops_.empty()) // ① 没 start 过，兜底
    return baseLoop_;
  EventLoop *loop = loops_[next_];
  next_ = (next_ + 1) % loops_.size(); // 环形递增
  return loop;
}