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

    // ★ 重新打包成 [4 字节网络序长度][body],跟协议约定一致
    uint32_t net_len = htonl(len);
    std::string out(sizeof(net_len), '\0');
    std::memcpy(out.data(), &net_len, sizeof(net_len));
    out.append(body);
    c->send(out);
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

/*
python3 test_conn.py 1000

ps -o nlwp,rss,cmd -C server
# NLWP   RSS CMD
# 1  18116 ./server
*/
