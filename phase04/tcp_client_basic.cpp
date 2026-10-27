#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/*
AI 先行解析提示：

服务器端就像总机，先准备好“电话线”并“监听”来电；客户端则主动“拨号”。

客户端流程：
socket() → connect() → send() → recv() → close()
socket()：创建通信端点。
connect()：向服务器的IP和端口发起连接请求（拨号）。
send() / recv()：连接成功后，发送和接收数据。
close()：关闭连接。
*/


int main() {
  // ---------- 1. socket() ----------
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket");
    return 1;
  }

  // ---------- 2. connect() ----------
  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(8080);

  // 点分十进制字符串 → 32 位网络字节序整数
  if (inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr) != 1) {
    fprintf(stderr, "inet_pton: invalid address\n");
    close(sockfd);
    return -1;
  }

  // connect 会触发三次握手
  if (connect(sockfd, reinterpret_cast<struct sockaddr *>(&servaddr),
              sizeof(servaddr)) < 0) {
    perror("connect");
    close(sockfd);
    return -1;
  }
  printf("[client] connected to 127.0.0.1:8080\n");

  // ---------- 3. send() ----------
  const std::string msg = "Hello TCP";
  ssize_t sent = send(sockfd, msg.data(), msg.size(), 0);
  if (sent < 0) {
    perror("send");
    close(sockfd);
    return -1;
  }
  printf("[client] sent: %s\n", msg.c_str());

  // ---------- 4. recv() ----------
  char buf[1024];
  ssize_t n = recv(sockfd, buf, sizeof(buf) - 1, 0);
  if (n < 0) {
    perror("recv");
    close(sockfd);
    return -1;
  }
  if (n == 0) {
    printf("[client] server closed before replying\n");
    close(sockfd);
    return 0;
    // return -1;
  }
  buf[n] = '\0';
  printf("[client] reply: %s\n", buf);

  // ---------- 5. close() ----------
  close(sockfd);
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