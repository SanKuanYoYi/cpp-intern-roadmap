#include "EpollPoller.h"

#include <cerrno> // errno, ENOENT, EINTR
#include <cstdint>
#include <stdexcept>
#include <sys/epoll.h>
#include <unistd.h> //close

EpollPoller::EpollPoller() {
  epfd_ = epoll_create1(EPOLL_CLOEXEC);
  if (epfd_ < 0)
    throw std::runtime_error("epoll_create1 failed");
}

EpollPoller::~EpollPoller() {
  if (epfd_ >= 0) {
    close(epfd_);
  }
}

void EpollPoller::update(int fd, uint32_t events) {
  struct epoll_event ev{};
  ev.events = events; // 调用方传的（EPOLLIN | EPOLLET 之类）
  ev.data.fd = fd;

  // 第一次尝试：MOD
  // 成功epoll_ctl()返回0。错误返回-1
  if (epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) == 0) {
    return;
  }

  // MOD 失败，检查是不是 ENOENT（fd 没注册过）
  if (errno == ENOENT) {
    // 是 → 说明第一次注册，用 ADD
    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) == 0) {
      return;
    }
  }

  // 两种都失败 → 抛异常
  throw std::runtime_error("epoll_ctl failed");
}

void EpollPoller::remove(int fd) {
  epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
  // EPOLL_CTL_DEL 时 event
  // 参数被内核忽略（删除不需要知道原事件内容）。现代内核都接受 nullptr。

  // 返回值故意忽略
}

int EpollPoller::wait(std::vector<struct epoll_event> &out, int timeoutMs) {
  int n =
      epoll_wait(epfd_, out.data(), static_cast<int>(out.size()), timeoutMs);

  if (n < 0) {
    if (errno == EINTR) {
      return 0; // 被信号打断，当作"这轮没有事件"
      // 关键：返回 0，不是 while 重试
    }
    throw std::runtime_error("epoll_wait failed");
  }

  return n;
}
