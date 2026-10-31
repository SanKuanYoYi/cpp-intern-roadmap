#include "mysql_conn.h"

#include <mysql/mysql.h>
#include <stdexcept>
#include <utility>

MySQLConnection::MySQLConnection(const char *host, const char *user,
                                 const char *password, const char *db) {
  conn_ = mysql_init(nullptr);
  if (!conn_) {
    throw std::runtime_error("mysql_init failed");
  }

  if (!mysql_real_connect(conn_, host, user, password, db, 0, nullptr, 0)) {
    std::string err = mysql_error(conn_); // errno != error
    mysql_close(conn_); // 连接失败也必须释放 mysql_init 分配的内存
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
    if (conn_) {
      mysql_close(conn_); // 释放"我"原来持有的连接
    }
    conn_ = std::exchange(other.conn_, nullptr); // 移动资源：拿走 other 的句柄
                                                 // exchange 返回前者旧值
  }
  return *this;
}

bool MySQLConnection::execute(const std::string &sql) {
  if (!conn_)
    return false;
  if (mysql_query(conn_, sql.c_str()) != 0) {
    return false;
  }
  return true;
}

MYSQL_RES *MySQLConnection::query(const std::string &sql) {
  if (!conn_)
    return nullptr;
  if (mysql_query(conn_, sql.c_str()) != 0) {
    return nullptr;
  }
  MYSQL_RES *result = mysql_store_result(conn_);
  if (!result) {
    // 若 mysql_field_count() == 0，说明语句本就不产生结果集，不算错
    // 但 query() 的契约是“执行 SELECT”，所以这里按出错处理
    return nullptr;
  }
  return result;
}

std::string MySQLConnection::last_error() const {
  if (!conn_)
    return "connection is null";
  return mysql_error(conn_);
}