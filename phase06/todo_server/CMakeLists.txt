cmake_minimum_required(VERSION 3.10)
project(todo_server CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)

add_executable(todo_server
    main.cpp
    http_parser.cpp
    http_response.cpp
    todo_repo.cpp
    mysql_conn.cpp
)
target_include_directories(todo_server PRIVATE ${MYSQL_INCLUDE_DIRS})
target_link_directories(todo_server PRIVATE ${MYSQL_LIBRARY_DIRS})
target_link_libraries(todo_server PRIVATE ${MYSQL_LIBRARIES})