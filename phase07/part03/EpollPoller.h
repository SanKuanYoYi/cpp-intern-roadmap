#pragma once

#include <cstdint> // uint32_t
#include <sys/epoll.h>
#include <vector>

class Channel; // ← 新增：前向声明，避免和 Channel.h 循环 include

class EpollPoller {
private:
  int epfd_ = -1;

public:
  EpollPoller();
  ~EpollPoller();

  EpollPoller(const EpollPoller &) = delete;
  EpollPoller &operator=(const EpollPoller &&) = delete;

  // 注册或修改关注的事件；内部自动决定 ADD 还是 MOD
  void update(Channel *ch); // ← 改

  // 从 epoll 中移除
  void remove(Channel *ch); // ← 改

  void poll(int timeoutMs, std::vector<Channel *> &active); // ← 改（原 wait）
};