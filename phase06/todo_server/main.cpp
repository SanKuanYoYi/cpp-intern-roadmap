#include "http_parser.h"
#include "http_response.h"
#include "todo_repo.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

// ---------- JSON 提取：从 body 里取 "title" 的值（简易实现） ----------
static std::string extract_json_string(const std::string &body,
                                       const std::string &key) {
  std::string needle = "\"" + key + "\""; // ← 构造 "title"
  auto pos = body.find(needle);
  if (pos == std::string::npos)
    return ""; // 没找到

  auto colon_pos = body.find(":", pos + needle.size()); // ← "title" 后的冒号
  if (colon_pos == std::string::npos)
    return ""; // 没找到

  auto quote_pos = body.find("\"", colon_pos + 1); // ← 冒号后的第一个引号
  if (quote_pos == std::string::npos)
    return ""; // 没找到

  auto end_quote_pos =
      body.find("\"", quote_pos + 1); // ← 第一个引号后的第二个引号
  if (end_quote_pos == std::string::npos)
    return ""; // 没找到

  std::string out;
  for (size_t i = quote_pos + 1; i < end_quote_pos;
       i++) { // 从第一个引号后的字符开始
    char c = body[i];
    if (c == '\\' && i + 1 < body.size()) {
      char next = body[i + 1];
      if (next == '"' || next == '\\' || next == '/') {
        out += next;
        i++; // 跳过下一个字符
      } else if (next == 'b') {
        out += '\b';
        i++;
      } else if (next == 'f') {
        out += '\f';
        i++;
      } else if (next == 'n') {
        out += '\n';
        i++;
      } else if (next == 'r') {
        out += '\r';
        i++;
      } else if (next == 't') {
        out += '\t';
        i++;
      } else {
        out += c; // 保留反斜杠
      }
    } else if (c == '"') {
      // 遇到引号，结束
      break;
    } else {
      out += c;
    }
  }
  return out;
}

// ---------- JSON 提取：从 body 里取一个整数字段的值 ----------
// 找不到、格式非法、非整数时返回 -1（done 合法值只有 0/1，-1 不会冲突）
static int extract_json_int(const std::string &body, const std::string &key) {
  std::string needle = "\"" + key + "\""; // ← 构造 "done"
  auto pos = body.find(needle);
  if (pos == std::string::npos)
    return -1; // 没找到

  auto colon_pos = body.find(":", pos + needle.size()); // ← "done" 后的冒号
  if (colon_pos == std::string::npos)
    return -1; // 没找到

  auto comma_pos = body.find(",", colon_pos + 1); // ← 冒号后的逗号
  auto end_pos = comma_pos == std::string::npos
                     ? body.find("}", colon_pos + 1)
                     : comma_pos; // ← 冒号后的逗号或右花括号
  if (end_pos == std::string::npos)
    return -1; // 没找到

  size_t start = colon_pos + 1;
  while (start < body.size() && (body[start] == ' ' || body[start] == '\t' ||
                                 body[start] == '\n' || body[start] == '\r')) {
    start++; // 跳过空白字符
  }

  if (start >= end_pos) {
    return -1; // 没有数字
  }

  try {
    return std::stoi(body.substr(start, end_pos - start));
  } catch (...) {
    return -1; // 非整数
  }
}

// ---------- 路由 ----------
static HttpResponse handle_request(const HttpRequest &request, TodoRepo &repo,
                                   const std::string &index_html) {
  HttpResponse response;
  const std::string &method = request.method;
  const std::string &path = request.path;

  // GET /
  if (method == "GET" && path == "/") {
    response.status = 200;
    response.content_type = "text/html; charset=utf-8";
    response.body = index_html;
    return response;
  }

  // /todos 集合
  if (path == "/todos") {
    if (method == "GET") {
      // GET /todos
      auto todos = repo.list();
      std::string body = "[";
      for (size_t i = 0; i < todos.size(); i++) {
        const auto &t = todos[i];
        body += "{\"id\":" + std::to_string(t.id) + ",\"title\":\"" +
                json_escape(t.title) + "\",\"done\":" + std::to_string(t.done) +
                "}";
        if (i + 1 < todos.size()) {
          body += ",";
        }
      }
      body += "]";
      response.status = 200;
      response.content_type = "application/json; charset=utf-8";
      response.body = body;
      return response;
    }
    if (method == "POST") {
      // POST /todos
      std::string title = extract_json_string(request.body, "title");
      if (title.empty()) {
        response.status = 400;
        response.content_type = "text/plain; charset=utf-8";
        response.body = "{\"error\":\"Missing title\"}";
        return response;
      }
      int new_id = repo.create(title);
      if (new_id < 0) {
        response.status = 500;
        response.content_type = "text/plain; charset=utf-8";
        response.body = "{\"error\":\"Failed to create todo\"}";
        return response;
      }
      response.status = 201;
      response.content_type = "application/json; charset=utf-8";
      response.body = "{\"id\":" + std::to_string(new_id) + ",\"title\":\"" +
                      json_escape(title) + "\",\"done\":0}";
      return response;
    }
    response.status = 405;
    response.content_type = "text/plain; charset=utf-8";
    response.body = "{\"error\":\"Method Not Allowed\"}";
    return response;
  }

  // /todos/:id
  if (path.rfind("/todos/", 0) == 0) {
    std::string id_str = path.substr(7);
    int id;
    try {
      id = std::stoi(id_str);
    } catch (...) {
      response.status = 400;
      response.body = "{\"error\":\"Invalid id\"}";
      return response;
    }
    if (method == "GET") {
      // GET /todos/:id
      auto todo_opt = repo.get(id);
      if (!todo_opt.has_value()) {
        response.status = 404;
        response.content_type = "text/plain; charset=utf-8";
        response.body = "{\"error\":\"Todo not found\"}";
        return response;
      }
      const auto &t = todo_opt.value();
      response.status = 200;
      response.content_type = "application/json; charset=utf-8";
      response.body = "{\"id\":" + std::to_string(t.id) + ",\"title\":\"" +
                      json_escape(t.title) +
                      "\",\"done\":" + std::to_string(t.done) + "}";
      return response;
    }
    if (method == "DELETE") {
      // DELETE /todos/:id
      int affected_rows = repo.remove(id);
      if (affected_rows < 0) {
        response.status = 500;
        response.content_type = "text/plain; charset=utf-8";
        response.body = "{\"error\":\"Failed to delete todo\"}";
        return response;
      }
      if (affected_rows == 0) {
        response.status = 404;
        response.content_type = "text/plain; charset=utf-8";
        response.body = "{\"error\":\"Todo not found\"}";
        return response;
      }
      response.status = 204;
      response.content_type = "text/plain; charset=utf-8";
      response.body = "";
      return response;
    }
    if (method == "PUT") {
      // PUT /todos/:id  body: {"done":0|1}
      int done = extract_json_int(request.body, "done");
      if (done != 0 && done != 1) {
        response.status = 400;
        response.content_type = "text/plain; charset=utf-8";
        response.body = "{\"error\":\"Invalid done value\"}";
        return response;
      }
      // 先确认存在（update_done 只看 SQL 是否成功，无法区分"不存在"和"值没变"）
      auto todo_opt = repo.get(id);
      if (!todo_opt.has_value()) {
        response.status = 404;
        response.content_type = "text/plain; charset=utf-8";
        response.body = "{\"error\":\"Todo not found\"}";
        return response;
      }
      if (!repo.update_done(id, done)) {
        response.status = 500;
        response.content_type = "text/plain; charset=utf-8";
        response.body = "{\"error\":\"Failed to update todo\"}";
        return response;
      }
      // 返回更新后的 todo
      auto updated_todo_opt = repo.get(id);
      if (!updated_todo_opt.has_value()) {
        response.status = 500;
        response.content_type = "text/plain; charset=utf-8";
        response.body = "{\"error\":\"Failed to retrieve updated todo\"}";
        return response;
      }
      response.status = 200;
      response.content_type = "application/json; charset=utf-8";
      const auto &t = updated_todo_opt.value();
      response.body = "{\"id\":" + std::to_string(t.id) + ",\"title\":\"" +
                      json_escape(t.title) +
                      "\",\"done\":" + std::to_string(t.done) + "}";
      return response;
    }
    response.status = 405;
    response.content_type = "text/plain; charset=utf-8";
    response.body = "{\"error\":\"Method Not Allowed\"}";
    return response;
  }

  response.status = 404;
  response.content_type = "text/plain; charset=utf-8";
  response.body = "{\"error\":\"Not Found\"}";
  return response;
}

// ---------- 工具：读文件 ----------
static std::string read_file(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return "<h1>index.html not found</h1>";
  }
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

static int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    return -1;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
  // 1. MySQL 连接 + 建表
  MySQLConnection conn("localhost", "root", "Mysql@2026", "intern_prep");
  conn.execute("SET NAMES utf8mb4");
  TodoRepo repo(conn);
  if (!repo.init()) {
    fprintf(stderr, "create table failed: %s\n", conn.last_error().c_str());
    return 1;
  }

  // 2. 读 index.html 到内存（启动时读一次）
  std::string index_html = read_file("index.html");

  // 3. 监听 socket（非阻塞）
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket");
    return 1;
  }

  int opt = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt");
    close(listen_fd);
    return 1;
  }

  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(8080);
  if (bind(listen_fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(listen_fd);
    return 1;
  }

  if (listen(listen_fd, SOMAXCONN) < 0) {
    perror("listen");
    close(listen_fd);
    return 1;
  }

  set_nonblocking(listen_fd);

  // 4. epoll：监听 fd 用 ET
  int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    perror("epoll_create1");
    close(listen_fd);
    return 1;
  }
  epoll_event ev{};
  ev.events = EPOLLIN | EPOLLET;
  ev.data.fd = listen_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
    perror("epoll_ctl");
    close(listen_fd);
    close(epoll_fd);
    return 1;
  }

  // 每个连接一个解析器
  std::unordered_map<int, std::unique_ptr<HttpParser>> parsers;

  printf("Server is running on http://localhost:8080\n");

  epoll_event events[64];
  while (true) {
    int n = epoll_wait(epoll_fd, events, 64, -1);

    if (n < 0) {
      if (errno == EINTR) {
        continue; // 被信号中断，继续等待
        perror("epoll_wait");
        break;
      }
    }

    for (int i = 0; i < n; i++) {
      int fd = events[i].data.fd;

      // ---- 新连接 ----
      if (fd == listen_fd) {
        while (true) { // ET：accept 到 EAGAIN
          sockaddr_in client_addr;
          socklen_t client_len = sizeof(client_addr);
          int client_fd =
              accept(listen_fd, (sockaddr *)&client_addr, &client_len);
          if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              break;
            }
            if (errno == EINTR) {
              continue;
            }
            perror("accept");
            break;
          }
          set_nonblocking(client_fd);
          epoll_event client_ev{};
          client_ev.events = EPOLLIN | EPOLLET;
          client_ev.data.fd = client_fd;
          epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev);
          parsers[client_fd] = std::make_unique<HttpParser>();
        }
        continue;
      }

      // ---- 已连接 socket 可读 ----
      auto it = parsers.find(fd);
      if (it == parsers.end()) {
        // 不在 parsers 中，可能是已关闭的连接
        continue;
      }

      HttpParser &parser = *(it->second);

      bool closed = false;
      while (true) { // ET：读到 EAGAIN
        char buf[4096];
        ssize_t nread = read(fd, buf, sizeof(buf));
        if (nread > 0) {
          if (parser.feed(buf, static_cast<size_t>(nread))) {
            // 解析完成，处理请求
            HttpResponse response =
                handle_request(parser.request(), repo, index_html);
            std::string response_str = to_write(response);
            send_all(fd, response_str); // Connection: close，发完就关
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
            close(fd);
            parsers.erase(it);
            closed = true;
            break;
          }
        } else if (nread == 0) {
          // 客户端关闭连接
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
          close(fd);
          parsers.erase(it);
          closed = true;
          break;
        } else {
          if (errno == EINTR)
            continue;
          if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
          close(fd);
          parsers.erase(it);
          closed = true;
          break;
        }
      }
    }
  }

  close(listen_fd);
  close(epoll_fd);
  return 0;
}

/*
运行结果：

<!-->
curl -v http://127.0.0.1:8080/
<-->
*   Trying 127.0.0.1:8080...
* Established connection to 127.0.0.1 (127.0.0.1 port 8080) from 127.0.0.1
port 43994
* using HTTP/1.x
> GET / HTTP/1.1
> Host: 127.0.0.1:8080
> User-Agent: curl/8.18.0
> Accept: * / *
>
* Request completely sent off
< HTTP/1.1 200 OK
< Content-Type: text/html; charset=utf-8
< Content-Length: 29
< Connection: close
<
* shutting down connection #0



<!-->
curl -X POST http://127.0.0.1:8080/todos \
     -H "Content-Type: application/json" \
     -d '{"title":"learn epoll"}'
<-->
{"id":1,"title":"learn epoll","done":0}



<!-->
curl http://127.0.0.1:8080/todos
<-->
[{"id":1,"title":"learn epoll","done":0}]



<!-->
curl http://127.0.0.1:8080/todos/1
<-->
{"id":1,"title":"learn epoll","done":0}



<!-->
curl -X POST http://127.0.0.1:8080/todos \
     -H "Content-Type: application/json" \
     -d $'{"title":"\'; DROP TABLE todos;--"}'
<-->
{"id":2,"title":"'; DROP TABLE todos;--","done":0}



<!-->
curl -s -X POST http://127.0.0.1:8080/todos \
     -H "Content-Type: application/json" \
     -d '{"title":"test"}'
<-->
{"id":4,"title":"test","done":0}



<!-->
curl -v -X PUT http://127.0.0.1:8080/todos/4 \
     -H "Content-Type: application/json" \
     -d '{"done":1}'
<-->
*   Trying 127.0.0.1:8080...
* Established connection to 127.0.0.1 (127.0.0.1 port 8080) from 127.0.0.1 port 50830 
* using HTTP/1.x
> PUT /todos/4 HTTP/1.1
> Host: 127.0.0.1:8080
> User-Agent: curl/8.18.0
> Accept: * / *
> Content-Type: application/json
> Content-Length: 10
> 
* upload completely sent off: 10 bytes
< HTTP/1.1 200 OK
< Content-Type: application/json; charset=utf-8
< Content-Length: 32
< Connection: close
< 
* shutting down connection #0
{"id":4,"title":"test","done":1}



<!-->
curl -v -X PUT http://127.0.0.1:8080/todos/999 \
     -H "Content-Type: application/json" \
     -d '{"done":1}'
<-->
*   Trying 127.0.0.1:8080...
* Established connection to 127.0.0.1 (127.0.0.1 port 8080) from 127.0.0.1 port 44704 
* using HTTP/1.x
> PUT /todos/999 HTTP/1.1
> Host: 127.0.0.1:8080
> User-Agent: curl/8.18.0
> Accept: * / *
> Content-Type: application/json
> Content-Length: 10
> 
* upload completely sent off: 10 bytes
< HTTP/1.1 404 Not Found
< Content-Type: text/plain; charset=utf-8
< Content-Length: 26
< Connection: close
< 
* shutting down connection #0
{"error":"Todo not found"}



<!-->
curl -v -X PUT http://127.0.0.1:8080/todos/5 \
     -H "Content-Type: application/json" \
     -d '{}'
<-->
*   Trying 127.0.0.1:8080...
* Established connection to 127.0.0.1 (127.0.0.1 port 8080) from 127.0.0.1 port 49356 
* using HTTP/1.x
> PUT /todos/5 HTTP/1.1
> Host: 127.0.0.1:8080
> User-Agent: curl/8.18.0
> Accept: * / *
> Content-Type: application/json
> Content-Length: 2
> 
* upload completely sent off: 2 bytes
< HTTP/1.1 400 Bad Request
< Content-Type: text/plain; charset=utf-8
< Content-Length: 30
< Connection: close
< 
* shutting down connection #0
{"error":"Invalid done value"}



<!-->
curl -v -X PUT http://127.0.0.1:8080/todos/abc -d '{"done":1}'
<-->
*   Trying 127.0.0.1:8080...
* Established connection to 127.0.0.1 (127.0.0.1 port 8080) from 127.0.0.1 port 55298 
* using HTTP/1.x
> PUT /todos/abc HTTP/1.1
> Host: 127.0.0.1:8080
> User-Agent: curl/8.18.0
> Accept: * / *
> Content-Length: 10
> Content-Type: application/x-www-form-urlencoded
> 
* upload completely sent off: 10 bytes
< HTTP/1.1 400 Bad Request
< Content-Type: text/plain; charset=utf-8
< Content-Length: 22
< Connection: close
< 
* shutting down connection #0
{"error":"Invalid id"}
*/