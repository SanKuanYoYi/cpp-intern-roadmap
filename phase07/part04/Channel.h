#pragma once

#include "EventLoop.h"

#include <cstdint>
#include <functional>
#include <sys/epoll.h>

class EventLoop;

class Channel {
public:
  using EventCallback = std::function<void()>;

  Channel(EventLoop *loop, int fd);
  ~Channel();

  Channel(const Channel &) = delete;
  Channel &operator=(const Channel &) = delete;

  int fd() const { return fd_; }
  uint32_t events() const { return events_; }
  void setRevents(uint32_t revents) { revents_ = revents; }

  void setReadCallback(EventCallback cb) { readCallback_ = std::move(cb); }
  void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
  void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
  void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

  void enableReading() {
    events_ |= EPOLLIN;
    update();
  }
  void disableReading() {
    events_ &= ~EPOLLIN;
    update();
  }
  void enableWriting() {
    events_ |= EPOLLOUT;
    update();
  }
  void disableWriting() {
    events_ &= ~EPOLLOUT;
    update();
  }
  void disableAll() {
    events_ = 0;
    update();
  }

  bool isWriting() const { return (events_ & EPOLLOUT) != 0; }

  void handleEvent();

private:
  void update() { loop->updateChannel(this); }

  EventLoop *loop;
  int fd_;
  uint32_t events_ = 0;
  uint32_t revents_ = 0;

  EventCallback readCallback_;
  EventCallback writeCallback_;
  EventCallback closeCallback_;
  EventCallback errorCallback_;
};