// 演示: 数据竞争 + std::mutex 修复
#include <thread>
#include <mutex>
#include <iostream>
#include <vector>



int counter = 0;
std::mutex mtx;         // ① 定义一把锁（全局，所有线程共用同一把）
//如果你在每个线程里各自定义一个 std::mutex，
//那等于每人一把独立的锁，谁也没拦住谁——起不到任何保护作用。



// ---- 不加锁版本（存在数据竞争）----
void increment_unsafe() {
    for (int i = 0; i < 10000; ++i) {
        ++counter;   // 非原子: load -> add -> store，中间可被打断
    }
}



// ---- 加锁版本（正确）----
void increment_safe() {
    for (int i = 0; i < 10000; ++i) {
        std::lock_guard<std::mutex> lk(mtx);    // ② 进门 + 锁门
        ++counter;                              // ③ 在"里面"改共享变量
    }                                           // ④ 出门 + 开锁（lk 析构自动做）
}
/*
std::lock_guard 是一个 RAII类，自动管理互斥量的锁定和解锁， 
确保在作用域结束时（无论是正常结束还是异常退出）互斥量被正确解锁
*/
/*
lock_guard 的构造函数里自动调用 mtx.lock() —— 也就是"拿钥匙 + 锁门"。
如果门已经被别人锁着（别人正在里面），mtx.lock() 会阻塞这个线程，
直到别人开门出来、把钥匙放下——然后才轮到自己锁门进去。
这一行 = "进入临界区"。
*/
/*
++counter; —— 在里面改共享数据
此刻保证只有你一个人在操作间里，没人能同时改 counter。
++counter 三步永远不会被另一个线程插队。
这段叫"临界区 (critical section)"——就是"必须独占执行"的代码。
*/
/*
右花括号 } —— 出门 + 开锁
当 lk 这个对象离开作用域（也就是走到 }）时：
lock_guard 的析构函数自动调用 mtx.unlock() —— 也就是"开门 + 把钥匙放下"。
这一行 = "离开临界区"。
关键点：你一行 unlock() 都不用写，全靠 lk 析构时自动完成。
*/



// 10 个线程并发跑一次，返回最终 counter
static int run(bool safe) {
    counter = 0;
    std::vector<std::thread> ts;
    ts.reserve(10);
    for (int i = 0; i < 10; ++i) {
        ts.emplace_back(safe ? increment_safe : increment_unsafe);
    }
    for (auto& t : ts) t.join();
    return counter;
}

int main() {
    std::cout << "期望值: " << 10 * 10000 << "\n";

    std::cout << "--- 不加锁 5 次 ---\n";
    for (int i = 1; i <= 5; ++i) {
        std::cout << "run " << i << ": " << run(false) << "\n";
    }

    std::cout << "--- 加锁 5 次 ---\n";
    for (int i = 1; i <= 5; ++i) {
        std::cout << "run " << i << ": " << run(true) << "\n";
    }
    return 0;
}

/*
输出结果：
期望值: 100000
--- 不加锁 5 次 ---
run 1: 87342
run 2: 91888
run 3: 85602
run 4: 92741
run 5: 89015
--- 加锁 5 次 ---
run 1: 100000
run 2: 100000
run 3: 100000
run 4: 100000
run 5: 100000
*/

/*
AI 解析：
原因：++counter 在机器层面是 
load   counter → 寄存器      ; ① 读：把 counter 的值从内存读进 CPU
add    1 → 寄存器            ; ② 加：在 CPU 里 +1
store  寄存器 → counter      ; ③ 写：把结果写回内存
三步操作。
两个线程可能同时 load 到相同旧值，分别 +1 后都写回，导致其中一次增加"丢失"。
这就是数据竞争 (data race)，属于未定义行为（UB）。

std::mutex = 一把门锁，锁的是一间"共享操作间"。
谁要改 counter，必须先拿到钥匙进门、锁门、改完、开门出来。
同一时刻只能有一个人在里面，其他人只能在门外排队等。
这样就没人能"同时改"，也就不会改乱。
只锁"必须独占的代码"（临界区）。锁越短，并发越高。能在锁外做的，就别放进来。

锁放循环外面
结果依然正确（100000），但用 time 命令测运行时间会发现：变成串行了，比锁在循环里慢很多。
这证明：mutex 不只是"正确性工具"，锁的范围直接决定性能。临界区越小越好是核心工程原则。
*/

/*
为什么用 lock_guard 而不是手动 lock()/unlock()
void increment_manual() {
    for (int i = 0; i < 10000; ++i) {
        mtx.lock();       // 手动锁
        ++counter;
        mtx.unlock();     // 手动开
    }
}这段也能跑对

问题 1：中间抛异常 → 永远不解锁
mtx.lock();
do_something_that_may_throw();   // ❌ 这里抛异常
mtx.unlock();                     // ← 这一行被跳过了！
异常会直接跳出函数，unlock() 永远没执行 → 锁被永久占着 → 其他所有线程全部死等。

问题 2：中途 return → 忘记解锁
mtx.lock();
if (cond) return;   // ❌ 直接走了，没 unlock
++counter;
mtx.unlock();
*/