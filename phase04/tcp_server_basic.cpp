#include "net_common.h"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>      //  open
#include <netinet/in.h> //IPv4 专用地址结构 + 字节序
#include <string>
#include <sys/socket.h> //socket 核心 API + 通用地址结构 + 地址族
#include <sys/types.h>
#include <unistd.h> //  close

/*
AI 先行解析提示：

基础流程：一次完整的“通话”
服务器端就像总机，先准备好“电话线”并“监听”来电；客户端则主动“拨号”。

服务器端流程：
socket() → bind() → listen() → accept() → recv() → send() → close()
socket()：创建一个通信端点（买一部电话）。
bind()：将端点绑定到特定的IP地址和端口（给电话分配一个号码）。
listen()：开始监听，准备接受连接（打开电话的铃声）。
accept()：阻塞等待，直到有客户端连接进来，
成功后返回一个新的套接字用于通信（电话铃响，拿起听筒，建立专门的通话线路）。
recv() / send()：在这个新连接上接收和发送数据（进行对话）。
close()：关闭连接（挂断电话）。
*/

int main() {
  int listenfd = create_listen_socket(8080);
  if (listenfd < 0) {
    perror("socket");
    return -1;
  }
  printf("[http] listening on 0.0.0.0:8080");

  int connfd = -1;
  do {
    connfd = accept(listenfd, nullptr, nullptr);
  } while (connfd < 0 && errno == EINTR);

  if (connfd < 0) {
    perror("accept");
    close(listenfd);
    return -1;
  }
  printf("[server] client connected, connfd=%d\n", connfd);

  char buf[1024];
  ssize_t n = recv(connfd, buf, sizeof(buf) - 1, 0);
  if (n < 0) {
    perror("recv");
    close(connfd);   // 关闭这条连接
    close(listenfd); // 停止监听
    return 1;
  }

  if (n == 0) { // 返回 0 不是错误，是对端关闭了连接
    printf("[server] peer closed\n");
    close(connfd);
    close(listenfd);
    return 0;
  }

  buf[n] = '\0';
  printf("[server] recv %zd bytes: %s\n", n, buf);

  std::string replay = std::string("echo: ") + buf;
  if (send_all(connfd, replay.data(), replay.size()) == false) {
    perror("send");
    close(connfd);
    close(listenfd);
    return -1;
  }
  printf("[server] connfd=%d sent %zu bytes\n", connfd, replay.size());

  close(connfd);   // 挂断这条连接（发 FIN，四次挥手开始）
  close(listenfd); // 停止监听

  return 0;
}

/*
# 终端 1：起服务器
./tcp_server_basic

# 终端 2：确认监听（新系统 netstat 已被 ss 取代，两个都能用）
netstat -tunlp | grep 8080
# 输出形如：tcp  0  0 0.0.0.0:8080  0.0.0.0:*  LISTEN  12345/./tcp_server_basic
ss -tlnp | grep 8080        # 等价命令

# 终端 3：发消息
./tcp_client_basic
# 客户端打印: reply: echo: hello tcp
*/