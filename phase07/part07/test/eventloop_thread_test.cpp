#include "EventLoop.h"
#include "EventLoopThread.h"
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>

int main() {
  EventLoopThread t;
  EventLoop *sub = t.startLoop();
  printf("main thread id = %zu\n",
         std::hash<std::thread::id>{}(std::this_thread::get_id()));

  // 实验 1：跨线程投递
  std::promise<void> p1;
  auto f1 = p1.get_future();
  sub->runInLoop([&p1] {
    printf("task ran in thread = %zu\n",
           std::hash<std::thread::id>{}(std::this_thread::get_id()));
    p1.set_value();
    ;
  });
  f1.wait();

  // 实验 2：wakeup —— 让子线程先睡死在 epoll_wait 里
  std::this_thread::sleep_for(std::chrono::seconds(1));
  std::promise<void> p2;
  auto f2 = p2.get_future();
  auto t0 = std::chrono::steady_clock::now();
  sub->runInLoop([&p2] { p2.set_value(); });
  f2.wait();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0)
                .count();
  printf("wakeup latency = %lld ms\n", (long long)ms);

  // 实验 3：本线程 runInLoop 应当同步执行
  printf("before sync\n");
  sub->runInLoop([] { printf("should NOT print from main thread\n"); });
  printf("after sync\n");
  return 0;
}