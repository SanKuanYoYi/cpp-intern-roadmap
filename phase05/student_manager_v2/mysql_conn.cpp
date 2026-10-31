#include "mysql_conn.h"

#include <mysql/mysql.h>
#include <stdexcept>
#include <vector>

MySQLConnection::MySQLConnection(const char *host, const char *user,
                                 const char *password, const char *db) {
  conn_ = mysql_init(nullptr);
  if (!conn_) {
    throw std::runtime_error("mysql_init failed");
  }

  if (!mysql_real_connect(conn_, host, user, password, db, 0, nullptr, 0)) {
    std::string err = mysql_error(conn_);
    mysql_close(conn_);
    conn_ = nullptr;
    throw std::runtime_error("connect failed: " + err);
  }
}

MySQLConnection::~MySQLConnection() {
  if (conn_) {
    mysql_close(conn_);
  }
}

MySQLConnection::MySQLConnection(MySQLConnection &&other) noexcept
    : conn_(std::exchange(other.conn_, nullptr)) {}

MySQLConnection &MySQLConnection::operator=(MySQLConnection &&other) noexcept {
  if (this != &other) {
    if (conn_)
      mysql_close(conn_);
    conn_ = std::exchange(other.conn_, nullptr);
  }
  return *this;
}

bool MySQLConnection::execute(const std::string &sql) {
  if (!conn_)
    return false;
  return mysql_query(conn_, sql.c_str()) == 0;
}

MYSQL_RES *MySQLConnection::query(const std::string &sql) {
  if (!conn_)
    return nullptr;
  if (mysql_query(conn_, sql.c_str()) != 0)
    return nullptr;
  return mysql_store_result(conn_);
}

std::string MySQLConnection::escape(const std::string &s) const {
  if (!conn_)
    return s;
  std::vector<char> buf(2 * s.size() + 1);
  unsigned long len =
      mysql_real_escape_string(conn_, buf.data(), s.c_str(), s.size());
  return std::string(buf.data(), len);
}

std::string MySQLConnection::last_error() const {
  if (!conn_)
    return "connection is null";
  return mysql_error(conn_);
}