#include "Buffer.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

int main() {

  Buffer b;

  assert(b.readableBytes() == 0);
  assert(b.writableBytes() == Buffer::kInitialSize);

  b.append("hello", 5);
  assert(std::string(b.peek(), b.readableBytes()) == "hello");

  b.retrieve(2);
  assert(std::string(b.peek(), b.readableBytes()) == "llo");

  b.append(std::string("world"));
  assert(std::string(b.peek(), b.readableBytes()) == "lloworld");

  b.retrieveAll();
  assert(b.readableBytes() == 0);

  std::string big(10000, 'x'); // 触发扩容
  b.append(big);
  assert(b.readableBytes() == 10000);

  // readFd：小包走 vec[0]
  int fds[2]; // 创建管道，读端 fds[0]，写端 fds[1]
  assert(pipe(fds) == 0);
  assert(write(fds[1], "abcdef", 6) == 6); // 往管道塞 6 字节
  b.retrieveAll();                         // 清空 b

  int err = 0;
  ssize_t n = b.readFd(fds[0], &err);
  assert(n == 6);
  assert(std::string(b.peek(), b.readableBytes()) == "abcdef");

  // readFd：超过可写空间，走栈上备用区 + append
  Buffer small(16); // 初始只有 16 字节
  std::string payload(100, 'y');
  assert(write(fds[1], payload.data(), payload.size()) ==
         100); // 管道里 100 字节
  n = small.readFd(fds[0], &err);
  assert(n == 100);
  assert(small.readableBytes() == 100);

  close(fds[0]);
  close(fds[1]);
  printf("buffer tests passed\n");
  return 0;
}