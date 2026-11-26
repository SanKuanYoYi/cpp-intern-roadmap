cmake_minimum_required(VERSION 3.16)
project(cpp_intern_roadmap_phase07 LANGUAGES CXX)

# ────────────────────────── 基础配置 ──────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)                # 不用 GNU 扩展，-std=c++17 而非 gnu++17

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)        # 生成 compile_commands.json，给 clangd 用
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})   # 可执行文件都在 build/ 下

find_package(Threads REQUIRED)               # T13 多线程要用

# ────────────────────────── 编译警告 ──────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wshadow                # 变量遮蔽（[&,raw] 里容易撞名）
    -Wconversion            # 隐式窄化，比如 ssize_t → size_t
    -Wno-unused-parameter   # 回调里未用的参数很常见
)

# ────────────────────────── 可选：ASan / UBSan ──────────────────────────
option(ENABLE_ASAN "Enable AddressSanitizer + UBSan" OFF)
if(ENABLE_ASAN)
    message(STATUS "ASan/UBSan enabled")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

# ────────────────────────── 网络库 ──────────────────────────
add_library(netlib STATIC
    src/Buffer.cpp
    src/Channel.cpp
    src/EpollPoller.cpp
    src/EventLoop.cpp
    src/EventLoopThread.cpp      
    src/TcpConnection.cpp
    src/TcpServer.cpp
)
target_include_directories(netlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(netlib PUBLIC Threads::Threads)

# ────────────────────────── 服务端 ──────────────────────────
add_executable(server src/main_V12.cpp)
target_link_libraries(server PRIVATE netlib)

# ────────────────────────── 测试程序 ──────────────────────────
# T13 的 eventloop_thread 测试
add_executable(eventloop_thread_test test/eventloop_thread_test.cpp)
target_link_libraries(eventloop_thread_test PRIVATE netlib)
add_test(NAME eventloop_thread_test COMMAND eventloop_thread_test)

enable_testing()
add_test(NAME buffer_test COMMAND buffer_test)



