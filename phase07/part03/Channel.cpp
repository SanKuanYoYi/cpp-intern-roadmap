#include "Channel.h"
#include "EpollPoller.h"

Channel::Channel(EpollPoller *poller, int fd) : poller_(poller), fd_(fd) {};
Channel::~Channel() = default;

void Channel::handleEvent() {
  // 顺序有讲究：错误 → 挂断 → 可读 → 可写
  if ((revents_ & EPOLLERR) && errorCallback_) {
    errorCallback_();
    return;
  }
  if ((revents_ & EPOLLHUP) && closeCallback_) {
    closeCallback_();
    return;
  }

  if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
    if (readCallback_)
      readCallback_();
  }
  if ((revents_ & EPOLLOUT) && writeCallback_) {
    writeCallback_();
  }
}