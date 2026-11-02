#include "todo_repo.h"
#include "mysql_conn.h"
#include <mysql/mysql.h>
#include <optional>
#include <string>
#include <vector>

TodoRepo::TodoRepo(MySQLConnection &conn) : conn_(conn) {}

bool TodoRepo::init() {
  return conn_.execute("CREATE TABLE IF NOT EXISTS todos ("
                       "id INT PRIMARY KEY AUTO_INCREMENT,"
                       "title VARCHAR(200) NOT NULL,"
                       "done TINYINT DEFAULT 0,"
                       "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)");
}

static Todo row_to_todo(MYSQL_ROW row) {
  Todo t;
  if (row[0])
    t.id = std::stoi(row[0]);
  if (row[1])
    t.title = row[1];
  if (row[2])
    t.done = std::stoi(row[2]);
  return t;
};

std::vector<Todo> TodoRepo::list() {
  std::vector<Todo> out;
  ResultSet rs(conn_.query("SELECT id, title, done FROM todos ORDER BY id"));
  if (!rs)
    return out;

  MYSQL_ROW row;
  while ((row = mysql_fetch_row(rs.get()))) {
    out.push_back(row_to_todo(row));
  }
  return out;
}

std::optional<Todo> TodoRepo::get(int id) {
  std::string sql =
      "SELECT id, title, done FROM todos WHERE id = " + std::to_string(id);
  ResultSet rs(conn_.query(sql));
  if (!rs) {
    return std::nullopt;
  }

  MYSQL_ROW row = mysql_fetch_row(rs.get());
  if (!row) {
    return std::nullopt;
  }
  return row_to_todo(row);
}

int TodoRepo::create(const std::string &title) {
  std::string escaped_title = conn_.escape(title);
  std::string sql =
      "INSERT INTO todos (title) VALUES ('" + escaped_title + "')";
  if (!conn_.execute(sql)) {
    return -1;
  }
  return static_cast<int>(mysql_insert_id(conn_.raw()));
}

int TodoRepo::remove(int id) {
  std::string sql = "DELETE FROM todos WHERE id = " + std::to_string(id);
  if (!conn_.execute(sql)) {
    return -1;
  }
  return static_cast<int>(mysql_affected_rows(conn_.raw()));
}