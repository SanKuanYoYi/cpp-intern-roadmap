// 编译：g++ -std=c++17 mysql_basic.cpp -o mysql_basic $(mysql_config --cflags --libs)

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

  MYSQL_RES *result = mysql_store_result(conn);
  if (!result) {
    fprintf(stderr, "store_result failed: %s\n", mysql_error(conn));
    mysql_close(conn);
    return 1;
  }

  MYSQL_ROW row;
  while ((row = mysql_fetch_row(result))) {
    int id = atoi(row[0]);
    int age = atoi(row[2]);
    double score = atof(row[3]);
    printf("id=%d, name=%s, age=%d, score=%.1f\n", id, row[1], age, score);
  }

  mysql_free_result(result);
  mysql_close(conn);
  return 0;
}

/*
id=1, name=Alice, age=18, score=90.0
id=2, name=Bob, age=19, score=85.0
id=3, name=Jack, age=20, score=80.0

是本地存储的结果
*/