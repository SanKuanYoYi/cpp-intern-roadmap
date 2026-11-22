#include "Buffer.h"
#include "Channel.h"
#include "EventLoop.h"
#include "TcpConnection.h"
#include "TcpServer.h"

#include <cstring>
#include <netinet/in.h>

// ══════════ 业务：切包 + 回写（唯一跟协议有关的地方）══════════
static void process_frames(TcpConnection *c, Buffer &in) {
  while (in.readableBytes() >= 4) {
    uint32_t len = 0;
    std::memcpy(&len, in.peek(), 4);
    len = ntohl(len); // 网络序 → 主机序

    if (len > 1024 * 1024) { // 单帧上限 1MB
      std::fprintf(stderr, "[proto] too large: %u\n", len);
      c->forceClose();
      return;
    }
    if (in.readableBytes() < 4u + len)
      break; // 半包，等下一批

    in.retrieve(4); // 吃掉长度头
    std::string body(in.peek(), len);
    in.retrieve(len); // 吃掉 body

    c->send(body); // echo
  }
}

// ══════════ main：只剩搭架子和业务 ══════════
int main() {
  EventLoop loop;                // ① 先有循环
  TcpServer server(&loop, 8080); // ② 再有服务器（依赖 loop）
  // server 后声明 → 先析构
  server.setMessageCallback(process_frames); // ③ 装业务
  // 签名顺序跟 MessageCallback 对齐(c、in)可直接写名
  server.start(); // ④ 开闸（socket 已在构造里建好）
  loop.loop();
  return 0; // 永远走不到
}

// cmake --build build -j

