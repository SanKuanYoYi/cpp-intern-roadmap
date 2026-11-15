#pragma once

#include <cstdint> // uint32_t
#include <sys/epoll.h>
#include <vector>

class EpollPoller {
private:
  int epfd_ = -1;

public:
  EpollPoller();
  ~EpollPoller();

  EpollPoller(const EpollPoller &) = delete;
  EpollPoller &operator=(const EpollPoller &&) = delete;

  // 注册或修改关注的事件；内部自动决定 ADD 还是 MOD
  void update(int fd, uint32_t events);

  // 从 epoll 中移除
  void remove(int fd);

  // 等待事件；返回就绪个数，事件写入 out（out 容量即最大返回数）
  int wait(std::vector<struct epoll_event> &out, int timeoutMs);
};