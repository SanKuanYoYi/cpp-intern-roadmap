#pragma once

#include <mysql/mysql.h>
#include <string>

class MySQLConnection {
private:
  MYSQL *conn_ = nullptr; // 默认初始化，防止未初始化指针

public:
  MySQLConnection(const char *host, const char *user, const char *password,
                  const char *db);
  ~MySQLConnection();

  // 禁止拷贝
  MySQLConnection(const MySQLConnection &) = delete;
  MySQLConnection &operator=(const MySQLConnection &) = delete;

  // 允许移动（noexcept）
  MySQLConnection(MySQLConnection &&) noexcept;
  MySQLConnection &operator=(MySQLConnection &&) noexcept;

  // 执行任意 SQL，返回 true 表示成功
  bool execute(const std::string &sql);

  // 执行 SELECT，返回结果集（调用方负责 mysql_free_result）
  // 失败返回 nullptr
  MYSQL_RES *query(const std::string &sql);

  // 获取错误信息
  std::string last_error() const;
};