#include "http_parser.h"

#include <cstring>
#include <sstream>
#include <string>

bool HttpParser::feed(const char *data, size_t len) {
  buffer_.append(data, len);

  // ---------- 阶段一：头部 ----------
  if (!headers_done_) {
    auto pos = buffer_.find("\r\n\r\n");
    if (pos == std::string::npos) {
      return false;
    }

    std::string head = buffer_.substr(0, pos);
    buffer_.erase(0, pos + 4);
    headers_done_ = true;

    std::istringstream iss(head);
    std::string line;

    std::getline(iss, line); // 请求行
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    std::istringstream request_line(line);
    request_line >> request_.method >> request_.path >> request_.version;

    while (std::getline(iss, line)) { // 头部
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      auto colon = line.find(':');
      if (colon == std::string::npos) {
        continue;
      }
      std::string key = line.substr(0, colon);
      std::string value = line.substr(colon + 1);
      // 去掉前导空格
      while (!value.empty() && value.front() == ' ') {
        value.erase(0, 1);
      }
      request_.headers[key] = value;
    }

    auto it = request_.headers.find("Content-Length");
    if (it != request_.headers.end()) {
      try {
        content_length_ = std::stoul(it->second);
      } catch (...) {
        content_length_ = 0;
        return false;
      }
    }
  }

  // ---------- 阶段二：body ----------
  if (buffer_.size() < content_length_) {
    return false;
  }

  request_.body = buffer_.substr(0, content_length_);
  buffer_.erase(0, content_length_);
  return true;
}
