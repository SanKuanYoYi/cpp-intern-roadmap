#pragma once

#include "EpollPoller.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class Channel;

class EventLoop {
public:
  using Functor = std::function<void()>;

  EventLoop();
  ~EventLoop();

  // 有 thread_id 和 mutex，拷贝无意义
  EventLoop(const EventLoop &) = delete;
  EventLoop &operator=(const EventLoop &) = delete;

  void runOnce(); // 跑一次
  void loop();    // 跑到天荒地老
  void quit();

  // 本线程 → 当场执行；其他线程 → 入队 + 按铃
  void runInLoop(Functor fn);   // 在 loop 线程就立刻跑，否则排队
  void queueInLoop(Functor fn); // 一律入队，必要时按铃

  bool isInLoopThread() const {
    return threadId_ == std::this_thread::get_id();
  }

  void updateChannel(Channel *ch) { poller_.update(ch); }
  void removeChannel(Channel *ch) { poller_.remove(ch); }
  // EventLoop::removeChannel 在 T12 变成了死代码

private:
  void doPendingFunctors();
  void wakeup();       // 按铃
  void handleWakeup(); // 接铃（注册在 poller_ 上）

  std::thread::id threadId_; // ① 最早初始化

  EpollPoller poller_; // ② 最早构造、最后析构
  std::vector<Channel *> active_;

  int wakeupFd_ = -1;                      // ④ 门铃 fd
  std::unique_ptr<Channel> wakeupChannel_; // ⑤ 依赖 poller_ 已就绪

  std::atomic<bool> quit_ = false;

  std::mutex mutex_; // 保护 pendingFunctors_
  std::vector<Functor> pendingFunctors_;
  std::atomic<bool> callingPendingFunctors_ = false;
};