#include "http_response.h"

#include <cerrno>
#include <cstdio>
#include <poll.h>
#include <string>
#include <sys/socket.h>

std::string status_text(int code) {
  switch (code) {
  case 200:
    return "OK";
  case 201:
    return "Created";
  case 204:
    return "No Content";
  case 400:
    return "Bad Request";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 413:
    return "Payload Too Large";
  case 500:
    return "Internal Server Error";
  default:
    return "Unknown";
  }
}

std::string to_write(const HttpResponse &response) {
  std::string out;
  out += "HTTP/1.1 " + std::to_string(response.status) + " " +
         status_text(response.status) + "\r\n";
  out += "Content-Type: " + response.content_type + "\r\n";
  out += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
  out += "Connection: close\r\n";
  for (const auto &header : response.extra_headers) {
    out += header.first + ": " + header.second + "\r\n";
  }
  out += "\r\n";
  out += response.body;
  return out;
}

std::string json_escape(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 8);

  for (unsigned char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "\\u%04x", c);
        out += buf;
      } else {
        out += static_cast<char>(c);
      }
    }
  }

  return out;
}

bool send_all(int fd, const std::string &data) {
  size_t total = 0;
  while (total < data.size()) {
    ssize_t n = send(fd, data.data() + total, data.size() - total, 0);
    if (n > 0) {
      total += static_cast<size_t>(n);
    } else if (n < 0) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        pollfd p{fd, POLLOUT, 0};
        if (poll(&p, 1, 5000) <= 0) {
          return false;
        }
        continue;
      }
      return false;
    } else {
      return false;
    }
    total += static_cast<size_t>(n);
  }
  return true;
}