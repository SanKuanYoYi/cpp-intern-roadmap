#pragma once

#include <cstddef>      //size_t
#include <sys/types.h>  //ssize_t

// 创建监听 socket 并 bind 到指定端口
// 成功返回 listenfd；失败返回 -1（errno 被设置，可 perror）
// 内部已设 SO_REUSEADDR，且调用 listen(fd, 128)
int create_listen_socket(int port);

// 循环 send 直到发完所有字节
// 成功返回 true（全部发送）；失败返回 false
bool send_all(int fd, const char *data, size_t len);

// 循环 recv 一次（内部处理 EINTR 重试）
// 返回值和 recv 语义一致：
//   > 0  实际读到的字节数
//   == 0 对端关闭
//   < 0  错误（非 EINTR，看 errno）
ssize_t recv_safe(int fd, void *buf, size_t len);