#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>

std::mutex mtx_A;
std::mutex mtx_B;

void thread1_func() {
    std::cout << "Thread 1 start" << std::endl;
    std::lock_guard<std::mutex> lockA(mtx_A); // 拿到 A
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::lock_guard<std::mutex> lockB(mtx_B); // 尝试拿 B，卡死
    std::cout << "Thread 1 done" << std::endl;
}

void thread2_func() {
    std::cout << "Thread 2 start" << std::endl;
    std::lock_guard<std::mutex> lockB(mtx_B); // 拿到 B
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::lock_guard<std::mutex> lockA(mtx_A); // 尝试拿 A，卡死
    std::cout << "Thread 2 done" << std::endl;
}

int main() {
    std::thread t1(thread1_func);
    std::thread t2(thread2_func);
    t1.join();
    t2.join();
    return 0;
}