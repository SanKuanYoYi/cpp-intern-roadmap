// g++ -O2 -std=c++17 -pthread test/bench.cpp -o build/bench

// 用法: ./bench <host> <port> <conns> <seconds>
// 每个连接一个线程：connect → 循环(发一帧、收一帧) → 统计

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

static std::atomic<long> g_roundtrips{0};

// 一帧: [4 字节大端 len][payload]
static bool send_frame(int fd, const char *payload, uint32_t len) {
  char buf[4 + 64];
  buf[0] = static_cast<char>((len >> 24) & 0xff);
  buf[1] = static_cast<char>((len >> 16) & 0xff);
  buf[2] = static_cast<char>((len >> 8) & 0xff);
  buf[3] = static_cast<char>(len & 0xff);
  memcpy(buf + 4, payload, len);
  ssize_t total = 4 + len;
  ssize_t sent = 0;
  while (sent < total) {
    ssize_t n = send(fd, buf + sent, static_cast<size_t>(total - sent), 0);
    if (n <= 0)
      return false;
    sent += n;
  }
  return true;
}

static bool recv_frame(int fd, uint32_t expectLen) {
  char buf[4 + 64];
  size_t got = 0;
  size_t need = 4 + expectLen;

  uint32_t got_len =
      (static_cast<uint32_t>(static_cast<uint8_t>(buf[0])) << 24) |
      (static_cast<uint32_t>(static_cast<uint8_t>(buf[1])) << 16) |
      (static_cast<uint32_t>(static_cast<uint8_t>(buf[2])) << 8) |
      static_cast<uint32_t>(static_cast<uint8_t>(buf[3]));
  if (got_len != expectLen)
    return false; // 协议对不上，立刻失败

  while (got < need) {
    ssize_t n = recv(fd, buf + got, need - got, 0);
    if (n <= 0)
      return false;
    got += static_cast<size_t>(n);
  }

  return true;
}

static void worker(const char *host, int port, int seconds) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port)); // 主机序 → 网络序（端口）
  inet_pton(AF_INET, host, &addr.sin_addr);           // 字符串 → 网络序（IP）

  if (connect(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
    fprintf(stderr, "[bench] connect failed: %s\n", strerror(errno));
    close(fd);
    return;
  }

  struct timeval tv{};
  tv.tv_sec = 5;
  tv.tv_usec = 0;
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

  const char *payload = "hello";
  uint32_t len = 5;
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  long local = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!send_frame(fd, payload, len))
      break;
    if (!recv_frame(fd, len))
      break;
    local++;
  }
  g_roundtrips.fetch_add(local);
  close(fd);
}

int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);

  if (argc != 5) {
    fprintf(stderr, "usage: %s host port conns seconds\n", argv[0]);
    return 1;
  }
  const char *host = argv[1];
  int port = atoi(argv[2]);
  int conns = atoi(argv[3]);
  int seconds = atoi(argv[4]);

  std::vector<std::thread> ts;
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < conns; i++) {
    ts.emplace_back(worker, host, port, seconds);
    for (auto &t : ts)
      t.join();
    double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
            .count();

    long rt = g_roundtrips.load();
    printf("conns=%d seconds=%.1f roundtrips=%ld qps=%.0f\n", conns, elapsed,
           rt, static_cast<double>(rt) / elapsed);

    return 0;
  }
}

/*
./bench 127.0.0.1 8080 1 10
# conns=1 seconds=10.0 roundtrips=414643 qps=41463
*/

/*
./build/server 8080 0 &
./build/bench 127.0.0.1 8080 200 10
kill %1
# conns=200 seconds=10.0 roundtrips=486108 qps=48609

pidstat -t -p $(pgrep -x server) 1
*/


/*
./build/server 8080 4 &
./build/bench 127.0.0.1 8080 200 10
kill %1
# conns=200 seconds=10.0 roundtrips=431855 qps=43184

pidstat -t -p $(pgrep -x server) 1
*/