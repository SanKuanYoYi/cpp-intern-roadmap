#include "http_parser.h"
#include "http_response.h"
#include "todo_repo.h"

#include <arpa/inet.h>
#include <cerrno>
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
    int id = std::stoi(id_str);
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
}