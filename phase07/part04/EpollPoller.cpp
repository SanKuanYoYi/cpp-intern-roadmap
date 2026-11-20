#include "EpollPoller.h"
#include "Channel.h"

#include <cerrno> // errno, ENOENT, EINTR
#include <stdexcept>
#include <sys/epoll.h>
#include <unistd.h> //close
#include <vector>

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

void EpollPoller::update(Channel *ch) {
  struct epoll_event ev{};
  ev.events = ch->events(); // ① 读 Channel 的"我想关注什么"
  ev.data.ptr = ch;         // ② 存对象指针，不是 fd
  int fd = ch->fd();        // ③ 读 fd 给 epoll_ctl 用

  if (ch->events() == 0) { // ④ 没有关注事件 → 直接摘
    remove(ch);
    return;
  }

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

void EpollPoller::remove(Channel *ch) {
  epoll_ctl(epfd_, EPOLL_CTL_DEL, ch->fd(), nullptr);
  // EPOLL_CTL_DEL 时 event
  // 参数被内核忽略（删除不需要知道原事件内容）。现代内核都接受 nullptr。

  // 返回值故意忽略
}

void EpollPoller::poll(int timeoutMs, std::vector<Channel *> &active) {
  struct epoll_event evs[64];
  int n = epoll_wait(epfd_, evs, 64, timeoutMs); // ① 等

  if (n < 0) {
    if (errno == EINTR) {
      return; // 被信号打断，当作"这轮没有事件"
      // 关键：返回 0，不是 while 重试
    }
    throw std::runtime_error("epoll_wait failed");
  }

  for (int i = 0; i < n; i++) {
    auto *ch = static_cast<Channel *>(evs[i].data.ptr); // ② 拿到对象
    ch->setRevents(
        evs[i]
            .events); // ← 内核告诉"这个 fd 实际就绪了什么" // ③ 记下"就绪什么"
    active.push_back(ch); // ④ 塞进列表
  }
}
