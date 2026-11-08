#include <arpa/inet.h>  // inet_ntop
#include <cstdio>       // printf, perror
#include <netinet/in.h> // sockaddr_in
#include <pthread.h>
#include <string>
#include <sys/socket.h> // socket
#include <unistd.h>     // read, write, close

// pthread 的接口强制要求参数是 void*（无类型指针）
void *handle_client(void *arg) {
  int fd = *(int *)arg; // 1. 拆箱：把 void* 强转回 int*，再取值
  delete (int *)arg;    // 2. 谁 new 谁 delete

  char buffer[4096];
  while (true) {
    ssize_t n = read(fd, buffer, sizeof(buffer));
    if (n == 0) {
      printf("Client disconnected\n");
      break;
    } else if (n < 0) {
      perror("read error");
      break;
    }
    std::string reply = "echo: ";
    reply.append(buffer, n);
    write(fd, reply.data(), reply.size());
  }
  close(fd);
  return nullptr;
}

int main() {
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket");
    return -1;
  }

  int opt = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(8080);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(listen_fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    return -1;
  }

  if (listen(listen_fd, 128) < 0) {
    perror("listen");
    return -1;
  }

  printf("Server is listening on port 8080...\n");

  while (true) {
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int conn_fd = accept(listen_fd, (sockaddr *)&client_addr, &client_len);
    if (conn_fd < 0) {
      perror("accept");
      continue;
    }

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
    printf("Accepted connection from %s:%d\n", ip, ntohs(client_addr.sin_port));

    // 必须 new：如果传 &conn_fd，下一轮 accept
    // 会把它覆盖掉，线程读到的就是别人的 fd
    int *pconn_fd = new int(conn_fd);
    pthread_t tid;
    pthread_create(&tid, nullptr, handle_client, pconn_fd);
    pthread_detach(tid);
  }
}

/*
A 终端已跑 server 且 ulimit -n 4096 已设。

B 终端：
python3 test/conn.py 100
# 输出：connected: 100

python3 test/conn.py 500
# 输出：connected: 500

python3 test/conn.py 1000
# 输出：connected: 1000

C 终端：
RSS 是什么？ → 进程真正占用的物理内存，单位 KB

ps -o pid,nlwp,rss,cmd -C server
# PID NLWP   RSS CMD
# 16836  101  5524 ./server

ps -o pid,nlwp,rss,cmd -C server
# PID NLWP   RSS CMD
# 16836  501 10848 ./server

ps -o pid,nlwp,rss,cmd -C server
# PID NLWP   RSS CMD
# 16836 1001 17404 ./server


*/