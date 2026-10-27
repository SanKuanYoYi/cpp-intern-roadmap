#include "net_common.h"

#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

// 静态文件根目录（相对进程工作目录，必须在 index.html 所在目录启动服务器）
static const std::string ROOT = ".";

// ------------------------------------------------------------------
// 读整个文件到 std::string，失败返回 false
// ------------------------------------------------------------------
static bool read_file(const std::string &path, std::string &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;

  std::ostringstream ss;
  ss << in.rdbuf(); // 一次读到 EOF
  if (in.bad())
    return false; // 真正的读错误
  // ifs.fail() && !ifs.eof() 也可能表示错误，但 rdbuf 场景下通常会到 eof
  out = ss.str();
  return true;
}

// ------------------------------------------------------------------
// 循环发完所有字节（send 可能部分发送，必须循环）
// ------------------------------------------------------------------
static bool send_all(int fd, const std::string &data) {
  ssize_t total = 0;
  while (total < data.size()) {
    ssize_t n =
        send(fd, data.data() + total, data.size() - total, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (n == 0)
      return false;
    total += n;
  }
  return true;
}

// ------------------------------------------------------------------
// 构造完整 HTTP 响应（头部 + body）
// ------------------------------------------------------------------
static std::string make_response(int code, const std::string &status,
                                 const std::string &content_type,
                                 const std::string &body) {
  std::ostringstream ss;
  ss << "HTTP/1.1 " << code << " " << status << "\r\n";
  ss << "Content-Type: " << content_type << "\r\n";
  ss << "Content-Length: " << body.size() << "\r\n";
  ss << "Connection: close\r\n";
  ss << "\r\n";
  ss << body;
  return ss.str();
}

// ------------------------------------------------------------------
// 处理一个客户端连接
// ------------------------------------------------------------------
static void handle_client(int connfd) {
  // ---------- 1. 读到 \r\n\r\n 为止（请求头结束标志） ----------
  // HTTP 请求可能在 TCP 上被拆成多个段，必须循环 recv
  std::string req; // ① 累积容器（跨多次 recv）
  char buf[1024];  // ② 单次接收缓冲（每次 recv 覆盖）
  while (req.find("\r\n\r\n") == std::string::npos) {
    ssize_t n = recv(connfd, buf, sizeof(buf), 0);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      perror("recv");
      close(connfd);
      return;
    }
    if (n == 0) {
      close(connfd);
      return;
    }
    req.append(buf, n);
    if (req.size() > 65536)
      break; // 防御：请求头异常大
  }

  // ---------- 2. 解析请求行 ----------
  size_t line_end = req.find("\r\n");
  if (line_end == std::string::npos) {
    send_all(connfd,
             make_response(400, "Bad Request", "text/plain", "bad request\n"));
    close(connfd);
    return;
  }
  std::string request_line = req.substr(0, line_end);
  printf("[http] request line: %s\n", request_line.c_str());

  std::istringstream iss(request_line);
  std::string method, path, version;
  iss >> method >> path >> version;
  if (method.empty() || path.empty() || version.empty()) {
    send_all(connfd, make_response(400, "Bad Request", "text/plain",
                                   "bad request line\n"));
    close(connfd);
    return;
  }

  // ---------- 3. 只支持 GET ----------
  if (method != "GET") {
    send_all(connfd, make_response(405, "Method Not Allowed", "text/plain",
                                   "method not allowed\n"));
    close(connfd);
    return;
  }

  // ---------- 4. 路由 ----------
  if (path == "/")
    path = "/index.html";

  // 安全：禁止 ../目录穿越。demo 演示
  if (path.find("..") != std::string::npos) {
    send_all(connfd,
             make_response(403, "Forbidden", "text/plain", "forbidden\n"));
    close(connfd);
    return;
  }

  // ---------- 5. 读文件 ----------
  std::string filepath = ROOT + path;
  std::string body;
  if (!read_file(filepath, body)) {
    send_all(connfd,
             make_response(404, "Not Found", "text/plain", "404 not found\n"));
    close(connfd);
    printf("[http] 404 %s\n", path.c_str());
    return;
  }

  // ---------- 6. 200 OK ----------
  // demo 简化：所有文件都当 html 返回（生产要后缀查）
  std::string resp = make_response(200, "OK", "text/html;charset=utf-8", body);
  send_all(connfd, resp);
  close(connfd);
  printf("[http] 200 %s (%zu bytes)\n", path.c_str(), body.size());
}

int main() {
  int listenfd = create_listen_socket(8080);
  if (listenfd < 0)
    return 1;
  printf("[http] listening on 0.0.0.0:8080, root=%s\n", ROOT.c_str());

  // 单线程串行处理（demo 够用；
  while (true) {
    int connfd;
    do {
      connfd = accept(listenfd, nullptr, nullptr);
    } while (connfd < 0 && errno == EINTR);

    if (connfd < 0) {
      perror("accept");
      break;
    }
    std::thread(handle_client, connfd).detach();
  }

  close(listenfd);
  return 0;
}

/*
运行结果：

# 终端 1
./http_server
# [http] listening on 0.0.0.0:8080, root=.

# 终端 2
curl -v http://127.0.0.1:8080/index.html
# < HTTP/1.1 200 OK
# < Content-Type: text/html; charset=utf-8
# < Content-Length: 234
# < Connection: close
# <
# <!DOCTYPE html>...

curl -v http://127.0.0.1:8080/
# 同 200（路由把 / 映射到 /index.html）

curl -v http://127.0.0.1:8080/nonexist.html
# < HTTP/1.1 404 Not Found
# < Content-Length: 14

curl -v -X POST http://127.0.0.1:8080/index.html
# < HTTP/1.1 405 Method Not Allowed
*/

/*
AI 解析:

HTTP 请求的格式是什么？
请求行\r\n
请求头1: 值\r\n
请求头2: 值\r\n
...
\r\n                       ← 空行（一个孤立的 \r\n）
请求体                       ← 可选，GET 一般为空

① 请求行（Request Line）——第一行
GET /index.html HTTP/1.1\r\n
└┬┘ └────┬─────┘ └──┬───┘
方法     请求目标    HTTP 版本
三个字段用单个空格（SP）分隔，不能是多个空格或 Tab（严格实现会拒绝）。
方法：GET、POST、PUT、DELETE、HEAD、OPTIONS…
请求目标：通常是一个路径 /index.html，也可能是绝对 URL（代理场景）
版本：HTTP/1.0、HTTP/1.1、HTTP/2（HTTP/2 是二进制，不用文本请求行）

② 请求头（Headers）——多行
Host: 127.0.0.1:8080\r\n
User-Agent: curl/8.5.0\r\n
Accept: *//*\r\n
Content-Length: 42\r\n
名称大小写不敏感：
值大小写敏感
同一头可以出现多次
冒号后允许一个空格

③ 空行 —— 一个孤立的 \r\n
这是 HTTP 文本协议最关键的一个字节序列——它标记"头部结束、body 开始"。

④ 请求体 —— 可选
GET：绝大多数情况没有 body（虽然协议不禁止）；
POST / PUT / PATCH：body 是提交的数据（表单、JSON、文件）；


Content-Length 有什么用？不写会怎样？
作用：声明 body 的字节数，让接收方知道"消息在哪里结束"

不写 Content-Length 会怎样

情形 1：响应中不写，Connection: close
客户端读到连接关闭才知道 body 结束：
能工作，但代价大：
每条响应必须关连接，无法复用；
客户端无法知道下载进度（不知道总大小）；
无法判断完整性——如果服务器中途崩溃，客户端看到的是"连接关了但内容不完整"，可能误以为成功。

情形 2：响应中不写，Connection: keep-alive
HTTP/1.1 规定这种响应是非法的——连接还要复用，body 又没边界，客户端根本不知道下一条响应从哪里开始。

情形 3：请求中不写（POST 等）
服务器不知道请求体多长：


为什么 HTTP 服务器通常用 Connection: keep-alive？你的服务器为什么用 close？
keep-alive 的好处
一次 TCP 连接处理多个 HTTP 请求/响应，而不是"一个请求一个 TCP 连接"：
省去反复三次握手
避免 TCP 慢启动
减少服务端 accept 和 close 次数——减轻内核调度
HTTP/1.1 默认 keep-alive——Connection: close 才是要显式写的例外

为什么 demo 服务器用 close
keep-alive 需要	  demo
精确解析每条请求的边界（Content-Length 或 chunked）	❌ 只读到 \r\n\r\n，不处理 body
循环处理同一连接上多个请求	❌ 一个连接处理一次就 close
客户端没发完请求时继续等	❌ 读完一次就 send
空闲超时（客户端保持连接不发请求多久后踢掉）	❌ 没实现，keep-alive 会 fd 泄漏
流式响应 / 分块响应	❌ 全在内存里拼好发一次

Connection: close 是"一种更简单的协议模式"——一条连接只服务一个请求


浏览器除了 GET /index.html 还会发什么？
访问 http://127.0.0.1:8080/index.html 后，浏览器会自动发第二个请求
[http] listening on 0.0.0.0:8080, root=.
[http] request line: GET /index.html HTTP/1.1
[http] 200 /index.html (293 bytes)
[http] request line: GET /favicon.ico HTTP/1.1
[http] 404 /favicon.ico

请求的是标签页的小图标。如果服务器没有这个文件，会返回 404——这完全正常，不是 bug。
*/