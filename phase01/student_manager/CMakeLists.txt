# 1. 声明 CMake 的最低版本要求
cmake_minimum_required(VERSION 3.10)

# 2. 定义项目名称和语言
project(StudentManager LANGUAGES CXX)

# 3. 设置 C++ 标准 (至少 C++11，推荐 C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. 添加可执行文件目标 (注意：不需要在这里列出 .h 头文件，但列出来也没坏处)
add_executable(student_manager
    main.cpp
    student.cpp
)

# 5. 包含当前目录 (虽然默认会包含，但显式声明更稳妥)
target_include_directories(student_manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 把源码目录下的 students.txt 复制到构建目录
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/students.txt ${CMAKE_CURRENT_BINARY_DIR}/students.txt COPYONLY)

