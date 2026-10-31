#include "student_manager.h"
#include "mysql_conn.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <mysql/mysql.h>
#include <sstream>
#include <stdexcept>
#include <string>

StudentManager::StudentManager(const char *host, const char *user,
                               const char *password, const char *db)
    : conn_(host, user, password, db) {
  // 确保表存在
  if (!conn_.execute("CREATE TABLE IF NOT EXISTS students ("
                     "id INT PRIMARY KEY AUTO_INCREMENT,"
                     "name VARCHAR(50) NOT NULL UNIQUE,"
                     "age INT NOT NULL,"
                     "score DOUBLE NOT NULL)")) {
    throw std::runtime_error("create table failed: " + conn_.last_error());
  }
}

void StudentManager::menu() {
  std::cout << "The Student Manager System" << std::endl;
  std::cout << "--------------------------------" << std::endl;
  std::cout << "1. Add Student" << std::endl;
  std::cout << "2. Delete Student" << std::endl;
  std::cout << "3. Modify Student" << std::endl;
  std::cout << "4. Search Student" << std::endl;
  std::cout << "5. Show All Students" << std::endl;
  std::cout << "6. Sort Students With Score" << std::endl;
  std::cout << "7. Clear All Students" << std::endl;
  std::cout << "8. Exit" << std::endl;
  std::cout << "--------------------------------" << std::endl;
}

void StudentManager::run() {
  while (true) {
    menu();
    int choice;
    std::cout << "Enter your choice: ";
    // 如果读取成功，std::cin 的状态是 good，转换为 true
    // 如果读取失败，std::cin 的状态被标记为 fail，转换为 false
    if (!(std::cin >> choice)) {
      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      std::cout << "Invalid choice. Please try again." << std::endl;
      continue;
    }
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    switch (choice) {
    case 1:
      add_student();
      break;
    case 2:
      delete_student();
      break;
    case 3:
      modify_student();
      break;
    case 4:
      search_student();
      break;
    case 5:
      show_all_students();
      break;
    case 6:
      sort_students();
      break;
    case 7:
      clear_all_students();
      break;
    case 8:
      std::cout << "Exiting the system." << std::endl;
      return;
    default:
      std::cout << "Invalid choice. Please try again." << std::endl;
    }
  }
}

// ---------- Add ----------
void StudentManager::add_student() {
  std::string name;
  int age;
  double score;

  std::cout << "Enter student name: ";
  std::getline(std::cin, name);
  std::cout << "Enter student age: ";
  if (!(std::cin >> age)) {
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cout << "Invalid age.\n" << std::endl;
    return;
  }

  std::cout << "Enter student score: ";
  if (!(std::cin >> score)) {
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cout << "Invalid score.\n" << std::endl;
    return;
  }

  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

  // ★ 关键：用 escape() 转义用户输入，O'Reilly 不会再破坏 SQL
  std::string escaped_name = conn_.escape(name);

  std::ostringstream oss;
  oss << "INSERT INTO students (name, age, score) VALUES ('" << escaped_name
      << "', " << age << ", " << score << ")";

  if (conn_.execute(oss.str())) {
    std::cout << "Student added successfully! (id="
              << mysql_insert_id(conn_.raw()) << ")\n" << std::endl;
  } else {
    std::cout << "Add failed: " << conn_.last_error() << "\n" << std::endl;
  }
}

// ---------- Delete ----------
void StudentManager::delete_student() {
  std::string name;
  std::cout << "Enter the name of the student to delete: ";
  std::getline(std::cin, name);
  std::string escaped = conn_.escape(name);

  std::string sql = "DELETE FROM students WHERE name = '" + escaped + "'";
  if (!conn_.execute(sql)) {
    std::cout << "Delete failed: " << conn_.last_error() << "\n" << std::endl;
    return;
  }

  if (mysql_affected_rows(conn_.raw()) == 0) {
    std::cout << "Student not found.\n" << std::endl;
  } else {
    std::cout << "Student deleted successfully!\n" << std::endl;
  }
}

// ---------- Modify ----------
void StudentManager::modify_student() {
  std::string name;
  std::cout << "Enter the name of the student to modify: ";
  std::getline(std::cin, name);
  std::string escaped = conn_.escape(name);

  int new_age;
  double new_score;
  std::cout << "Enter new age: ";
  std::cin >> new_age;
  std::cout << "Enter new score: ";
  std::cin >> new_score;
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

  std::ostringstream oss;
  oss << "UPDATE students SET age = " << new_age << ", score = " << new_score
      << " WHERE name = '" << escaped << "'";

  if (!conn_.execute(oss.str())) {
    std::cout << "Modify failed: " << conn_.last_error() << "\n" << std::endl;
    return;
  }

  if (mysql_affected_rows(conn_.raw()) == 0) {
    std::cout << "Student not found (or no change).\n" << std::endl;
  } else {
    std::cout << "Student modified successfully!\n" << std::endl;
  }
}

// ---------- Search ----------
void StudentManager::search_student() {
  std::string name;
  std::cout << "Enter the name of the student to search: ";
  std::getline(std::cin, name);
  std::string escaped = conn_.escape(name);

  std::string sql = "SELECT id, name, age, score FROM students WHERE name = '" +
                    escaped + "'";

  ResultSet rs(conn_.query(sql));
  if (!rs) {
    std::cout << "Search failed: " << conn_.last_error() << "\n" << std::endl;
    return;
  }

  MYSQL_ROW row = mysql_fetch_row(rs.get());
  if (!row) {
    std::cout << "Student not found.\n" << std::endl;
    return;
  }

  // ★ 类型转换：id/age 用 stoi，score 用 stod，NULL 用 nullptr 判断
  if (row[0] == nullptr) {
    std::cout << "id is NULL\n";
    return;
  }
  int id = std::atoi(row[0]);
  std::string n = row[1] ? row[1] : "(null)";
  int age = std::atoi(row[2]);
  double score = std::stod(row[3]);

  std::cout << "Student found: id=" << id << ", Name: " << n << ", Age: " << age
            << ", Score: " << score << std::endl
            << std::endl;
}

// ---------- Show All ----------
void StudentManager::show_all_students() {
  ResultSet rs(conn_.query("SELECT id, name, age, score FROM students"));
  if (!rs) {
    std::cout << "Query failed: " << conn_.last_error() << "\n" << std::endl;
    return;
  }

  int count = 0;
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(rs.get()))) {
    int id = std::stoi(row[0]);
    std::string n = row[1] ? row[1] : "(null)";
    int age = std::stoi(row[2]);
    double score = std::stod(row[3]);
    std::cout << "id=" << id << ", Name: " << n << ", Age: " << age
              << ", Score: " << score << std::endl;
    ++count;
  }
  std::cout << "\nThere are " << count << " students in the system.\n"
            << std::endl;
}

// ---------- Sort（ORDER BY 交给 MySQL 做） ----------
void StudentManager::sort_students() {
  ResultSet rs(conn_.query(
      "SELECT id, name, age, score FROM students ORDER BY score DESC"));
  if (!rs) {
    std::cout << "Query failed: " << conn_.last_error() << "\n" << std::endl;
    return;
  }

  MYSQL_ROW row;
  while ((row = mysql_fetch_row(rs.get()))) {
    std::cout << "id=" << std::stoi(row[0])
              << ", Name: " << (row[1] ? row[1] : "(null)")
              << ", Age: " << std::stoi(row[2])
              << ", Score: " << std::stod(row[3]) << std::endl;
  }
  std::cout << "\nStudents sorted by score in descending order.\n" << std::endl;
}

// ---------- Clear All ----------
void StudentManager::clear_all_students() {
  std::cout << "Are you sure you want to clear all students? (y/n): ";
  std::string answer;
  std::getline(std::cin, answer);
  if (answer != "y" && answer != "Y") {
    std::cout << "Cancelled.\n" << std::endl;
    return;
  }
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

  if (!conn_.execute("DELETE FROM students")) {
    std::cout << "Clear failed: " << conn_.last_error() << "\n" << std::endl;
    return;
  }

  std::cout << "All students cleared. Affected rows: "
            << mysql_affected_rows(conn_.raw()) << "\n"
            << std::endl;
}
