#pragma once

#include "mysql_conn.h"
#include <optional>
#include <string>
#include <vector>

struct Todo {
  int id = 0;
  std::string title;
  int done = 0;
};

class TodoRepo {
private:
  MySQLConnection &conn_;

public:
  explicit TodoRepo(MySQLConnection &conn);

  bool init();                          // CREATE TABLE IF NOT EXISTS
  std::vector<Todo> list();             // 全部
  std::optional<Todo> get(int id);      // 单个，不存在返回 nullopt
  int create(const std::string &title); // 返回新建的 id，失败 -1
  int remove(int id);                   // 删除，返回影响行数，-1 失败
  bool update_done(int id, int done);        // 更新，返回是否成功
};
