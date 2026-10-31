// 编译：g++ -std=c++17 mysql_basic_v2.cpp mysql_conn.cpp -o mysql_basic_v2 \ $(mysql_config --cflags --libs)

#include "mysql_conn.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <mysql/mysql.h>

int main() {
  try {
    MySQLConnection conn("localhost", "root", "Mysql@2026", "intern_prep");

    MYSQL_RES *result = conn.query("SELECT id, name, age, score FROM students");
    if (!result) {
      fprintf(stderr, "query failed: %s\n", conn.last_error().c_str());
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
    return 0;
  } catch (const std::exception &e) {
    fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}