#include "student_manager.h"

#include <iostream>

int main() {
  try {
    StudentManager manager("localhost", "root", "Mysql@2026", "intern_prep");
    manager.run();
  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}