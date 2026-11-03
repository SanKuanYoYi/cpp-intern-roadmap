#pragma once

#include <string>
#include <unordered_map>

struct HttpResponse {
  int status = 200;
  std::string content_type = "text/plain; charset=utf-8";
  std::string body;
  std::unordered_map<std::string, std::string> extra_headers;
};

// 状态码 -> "200 OK"
std::string status_text(int status);

// 把 HttpResponse 序列化成 HTTP/1.1 字节流
std::string to_write(const HttpResponse &response);

// JSON 字符串转义：", \, 控制字符
std::string json_escape(const std::string &s);

// 循环 send 直到全部发完；EAGAIN 时 poll 等待可写
bool send_all(int fd, const std::string &data);