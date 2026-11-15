#include "Channel.h"
#include "EpollPoller.h"

Channel::Channel(EpollPoller *poller, int fd) : poller_(poller), fd_(fd) {};
Channel::~Channel() = default;

void Channel::handleEvent() {
  // 顺序有讲究：错误 → 挂断 → 可读 → 可写
  if ((revents_ & EPOLLERR) && errorCallback_) {
    //"内核说这个 fd 出错了（EPOLLERR）？而且我贴了错门铃？→
    // 按错门铃，然后别管别的，退出。"
    errorCallback_();
    return;
  }
  if ((revents_ & EPOLLHUP) && closeCallback_) {
    //"对端彻底关了（EPOLLHUP）？贴了关门铃？→ 按关门铃，退出。"
    closeCallback_();
    return;
  }

  if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
    //"有数据可读（EPOLLIN）？贴了读门铃？→ 按读门铃。不
    // return，因为可能同时还要写。"
    if (readCallback_)
      readCallback_();
  }
  if ((revents_ & EPOLLOUT) && writeCallback_) {
    //"可写了（EPOLLOUT）？贴了写门铃？→ 按写门铃。"
    writeCallback_();
  }
}