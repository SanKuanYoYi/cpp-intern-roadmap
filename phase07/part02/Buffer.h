#pragma once

#include <cstddef>
#include <string>
#include <vector>

class Buffer {
private:
  char *begin();
  const char *begin() const;
  void ensureWritableBytes(size_t len);
  void hasWritten(size_t len);

  std::vector<char> buf_;
  size_t writeIndex_;
  size_t readIndex_;

public:
  static const size_t kCheapPrepend = 8; // 头部预留，方便将来加协议头
  static const size_t kInitialSize = 1024;

  explicit Buffer(size_t initialSize = kInitialSize);

  size_t readableBytes() const;    // 可读 = 还没被取走
  size_t writableBytes() const;    // 可写 = 尾部剩余空间
  size_t prependableBytes() const; // 头部空余

  const char *peek() const; // 指向第一个可读字节

  void retrieve(size_t len); // 取走 len 字节
  void retrieveAll();

  void append(const char *data, size_t len);
  void append(const std::string &s);

  // 从 fd 读进缓冲区：>0 读到字节数，0 对端关闭，-1 出错(errno 存 savedErrno)
  ssize_t readFd(int fd, int *savedErrno);
};