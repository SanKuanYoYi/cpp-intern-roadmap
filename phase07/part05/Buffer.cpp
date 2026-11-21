#include "Buffer.h"
#include <cstddef>
#include <sys/types.h>
#include <sys/uio.h> //struct iovec

// public:
Buffer::Buffer(size_t initialSize)
    : buf_(kCheapPrepend + initialSize), readIndex_(kCheapPrepend),
      writeIndex_(kCheapPrepend) {}

size_t Buffer::readableBytes() const { return writeIndex_ - readIndex_; }

size_t Buffer::writableBytes() const { return buf_.size() - writeIndex_; }

size_t Buffer::prependableBytes() const { return readIndex_; }

const char *Buffer::peek() const { return buf_.data() + readIndex_; }

void Buffer::retrieve(size_t len) {
  if (len < readableBytes()) {
    readIndex_ += len; // 只挪游标，数据不动
  } else {
    retrieveAll(); // 取走 >= 可读量 → 全清
  }
}

void Buffer::retrieveAll() {
  readIndex_ = kCheapPrepend; // ← 归到 8，不是 0
  writeIndex_ = kCheapPrepend;
}

ssize_t Buffer::readFd(int fd, int *saveErrno) {
  char extraBuf[65536]; // 栈上 64KB 备用区
  struct iovec vec[2];
  const size_t writable = writableBytes(); // 先存，append 后会变

  vec[0].iov_base = begin() + writeIndex_; // 第一块：Buffer 内部可写区
  vec[0].iov_len = writable;               //   长度 = 还剩多少可写

  vec[1].iov_base = extraBuf;        // 第二块：栈上 64KB
  vec[1].iov_len = sizeof(extraBuf); //   长度 = 65536

  const int iovcnt = (writable < sizeof(extraBuf)) ? 2 : 1;
  // 如果 Buffer 可写空间已经 ≥ 64KB，extrabuf 没用，只传 1 块

  const ssize_t n = readv(fd, vec, iovcnt);

  if (n < 0) {
    *saveErrno = errno; // 错误，交给调用方判断 EAGAIN/EINTR
  } else if (static_cast<size_t>(n) <= writable) {
    writeIndex_ += n; // 全读进 vec[0]，直接推进游标
  } else {
    // n > writable：vec[0] 被填满，多出来的在 extrabuf
    writeIndex_ = buf_.size(); // 先"假装"写满
    append(extraBuf,
           static_cast<size_t>(
               n - writable)); // 再 append 剩余（内部会 ensure/resize）
  }
  return n;
}

void Buffer::append(const char *data, size_t len) {
  ensureWritableBytes(len);                           // 保证有地方
  std::copy(data, data + len, begin() + writeIndex_); // 写在 w 处，不碰可读区
  hasWritten(len);
}

void Buffer::append(const std::string &s) { append(s.data(), s.size()); }

// private:
char *Buffer::begin() { return buf_.data(); }
const char *Buffer::begin() const { return buf_.data(); }

void Buffer::ensureWritableBytes(size_t len) {
  if (writableBytes() < len) {
    makeSpace(len); // 空间不够，想办法腾
  }
  // 够的话直接返回，什么都不做
}

void Buffer::makeSpace(size_t len) {
  if (writableBytes() + prependableBytes() < len + kCheapPrepend) {
    // 情况③：即使把可读数据挪到最前面，也不够 → 扩容
    buf_.resize(writeIndex_ + len);
  } else {
    // 情况②：压缩就够 → memmove 可读数据到 kCheapPrepend 位置
    size_t readable = readableBytes();
    std::copy(begin() + readIndex_, begin() + writeIndex_,
              begin() + kCheapPrepend);
    readIndex_ = kCheapPrepend;
    writeIndex_ = readIndex_ + readable;
  }
  // 情况①（writableBytes >= len）压根不会进 makeSpace
}

void Buffer::hasWritten(size_t len) { writeIndex_ += len; }
