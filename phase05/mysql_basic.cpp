#include <cstdio>
#include <cstdlib>
#include <mysql/mysql.h>

int main() {
  MYSQL *conn = mysql_init(nullptr);
  if (!conn) {
    fprintf(stderr, "mysql_init failed\n");
    return 1;
  }

  if (!mysql_real_connect(conn, "localhost", "root", "Mysql@2026",
                          "intern_prep", 0, nullptr, 0)) {
    fprintf(stderr, "connect failed: %s\n", mysql_error(conn));
    mysql_close(conn);
    return 1;
  }

  if (mysql_query(conn, "SELECT id, name, age, score FROM students")) {
    fprintf(stderr, "query failed: %s\n", mysql_error(conn));
    mysql_close(conn);
    return 1;
  }

  
}