#pragma once

#include <string>
#include <unordered_map>

struct HttpRequest {
  std::string method;
  std::string path;
  std::string version;
  std::unordered_map<std::string, std::string> headers;
  std::string body;
};

class HttpParser {
private:
  std::string buffer_;
  HttpRequest request_;
  bool headers_done_ = false;
  size_t content_length_ = 0;

public:
  // 返回 true 表示一个完整请求已解析完毕
  bool feed(const char *data, size_t len);
  const HttpRequest &request() const { return request_; }
  void reset();
};