#pragma once

#include <mysql/mysql.h>
#include <string>
#include <utility>

class MySQLConnection {
private:
  MYSQL *conn_ = nullptr;

public:
  MySQLConnection(const char *host, const char *user, const char *password,
                  const char *db);
  ~MySQLConnection();

  MySQLConnection(const MySQLConnection &) = delete;
  MySQLConnection &operator=(const MySQLConnection &) = delete;

  MySQLConnection(MySQLConnection &&other) noexcept;
  MySQLConnection &operator=(MySQLConnection &&other) noexcept;

  bool execute(const std::string &sql);
  MYSQL_RES *query(const std::string &sql);

  // 用 mysql_real_escape_string 转义字符串，防止 SQL 注入
  std::string escape(const std::string &s) const;

  std::string last_error() const;
  MYSQL *raw() const { return conn_; }
};

// MYSQL_RES 的 RAII 包装：析构时自动 mysql_free_result
class ResultSet {
private:
  MYSQL_RES *res_ = nullptr;

public:
  explicit ResultSet(MYSQL_RES *res) : res_(res) {}
  ~ResultSet() {
    if (res_)
      mysql_free_result(res_);
  }

  ResultSet(const ResultSet &) = delete;
  ResultSet &operator=(const ResultSet &) = delete;

  ResultSet(ResultSet &&other) noexcept
      : res_(std::exchange(other.res_, nullptr)) {}
  ResultSet &operator=(ResultSet &&other) noexcept {
    if (this != &other) {
      if (res_)
        mysql_free_result(res_);
      res_ = std::exchange(other.res_, nullptr);
    }
    return *this;
  }

  // 把内部封装的裸指针 res_ 原样返回，供那些只接受 MYSQL_RES* 的 C API 函数使用
  // MySQL C API 的所有函数签名都是非 const 的
  MYSQL_RES *get() const { return res_; }

  // 让 ResultSet 对象能直接用在 if、while、!、&&、|| 这些布尔上下文里，语义是"结果集是否有效"
  // eg.if (!rs)  //  while(rs)
  // explict 允许对象在 if/while/! 等明确需要布尔值的上下文里转换，但禁止它被悄悄转成 int、参与算术运算或和数字比较
  explicit operator bool() const { return res_ != nullptr; }
};