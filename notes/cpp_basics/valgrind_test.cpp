/*
！！故意制造的内存错误用于学习 ！！
*/
#include <iostream>

int* createArray() {
    int* arr = new int[5]; // 分配 5 个 int
    return arr;
}

int main() {
    // 1. 内存泄漏（new 了但没 delete）
    int* leaked = new int[10];

    // 2. 越界写（数组大小是 5，却写了 index 10）
    int* arr = createArray();
    arr[10] = 42; // 越界！

    // 3. 未初始化读（分配了内存但没赋值，直接读）
    int* uninit = new int[3];
    if (uninit[1] == 0) { // 读未初始化的值
        std::cout << "uninit[1] is zero" << std::endl;
    }

    // 4. 双重释放（释放两次）
    int* doubleFree = new int(5);
    delete doubleFree;
    delete doubleFree; // 再次释放，程序可能崩溃

    delete[] arr; // 正常释放
    // 注意：leaked 和 uninit 没有释放，是内存泄漏
    return 0;
}