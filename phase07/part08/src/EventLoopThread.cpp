#include "EventLoopThread.h"
#include "EventLoop.h"
#include <mutex>
#include <thread>

EventLoopThread::EventLoopThread() = default;

EventLoopThread::~EventLoopThread() {
  EventLoop *loop = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!thread_.joinable()) {
      return; // 从没调过 startLoop
    }
    loop = loop_; // 此时必然非空（见说明）
  }

  loop->quit();   // ① 置退出标志 + 按铃
  thread_.join(); // ② 等子线程真正退出
}

void EventLoopThread::threadFunc() {
  EventLoop loop; // ← 必须在线程内构造

  {
    std::lock_guard<std::mutex> lock(mutex_);
    loop_ = &loop;
  }
  cond_.notify_one(); // 通知 startLoop：都建好了

  loop.loop(); // 阻塞在这里，直到 quit()
}

EventLoop *EventLoopThread::startLoop() {
  thread_ = std::thread([this] { threadFunc(); });

  EventLoop *loop = nullptr;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] { return loop_ != nullptr; });
    loop = loop_;
  }

  return loop;
}