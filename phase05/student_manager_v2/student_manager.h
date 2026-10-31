#pragma once

#include "mysql_conn.h"
#include <string>

class StudentManager {
public:
  StudentManager(const char *host, const char *user, const char *password,
                 const char *db);

  void run();

private:
  void menu();
  void add_student();
  void delete_student();
  void modify_student();
  void search_student();
  void show_all_students();
  void sort_students();
  void clear_all_students();

  MySQLConnection conn_;
};