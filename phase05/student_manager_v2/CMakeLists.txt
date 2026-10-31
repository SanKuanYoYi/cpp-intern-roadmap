cmake_minimum_required(VERSION 3.10)
project(student_manager_mysql CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)

add_executable(student_manager
    main.cpp
    student_manager.cpp
    mysql_conn.cpp
)

target_include_directories(student_manager PRIVATE ${MYSQL_INCLUDE_DIRS})
target_link_directories(student_manager PRIVATE ${MYSQL_LIBRARY_DIRS})
target_link_libraries(student_manager PRIVATE ${MYSQL_LIBRARIES})