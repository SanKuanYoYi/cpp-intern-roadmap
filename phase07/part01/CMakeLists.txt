cmake_minimum_required(VERSION 3.10)
project(reactor_server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -g)


# 所有以 target_xxx 开头的命令（如 target_include_directories, target_compile_options）
# 都必须写在创建该目标的命令（add_executable / add_library）之后


add_executable(server
    main.cpp
    event_loop.cpp
    channel.cpp
    poller.cpp
    epoll_poller.cpp
)

find_package(Threads REQUIRED) # 让 CMake 自动去系统里找最适合的线程库
target_link_libraries(server PRIVATE Threads::Threads) # 把这个线程库链接到你的 server 可执行文件上
