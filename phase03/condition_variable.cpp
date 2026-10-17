// 经典生产者-消费者：队列 + mutex + condition_variable + done 标志
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <iostream>
#include <chrono>

std::queue<int> q;                     // 共享队列（受 mtx 保护）
std::mutex mtx;                        // 保护 q 和 done 的锁
std::condition_variable cv;            // "有新东西 / 有变化"的通知器
bool done = false;                     // 生产者是否已经结束
/*
std::queue<int> q	装数据（共享资源）	自身没有任何线程安全保证，必须靠 mtx 保护
std::mutex mtx	保证同一时刻只有一个人改 q / done	不知道"什么时候有新数据"
std::condition_variable cv	"有新数据/状态变了"的通知器	不存数据、不持锁，只是"敲门铃"
bool done	生产者结束信号	也必须受 mtx 保护（否则消费者可能读到旧值）
*/
//AI 提示：
//最容易错的认知：condition_variable 不是队列、不是信号量、不存东西。
//它只是"让一个线程挂起、让另一个线程叫醒它"的机制



// ---------------- 生产者 ----------------
void producer() {
    for (int i = 1; i <= 10; ++i) {
        {
            std::lock_guard<std::mutex> lk(mtx);   // 进入临界区
            q.push(i);                              // 修改共享状态
            std::cout << "[producer] push " << i << "\n";
        }                                          // ← 离开作用域，锁自动释放

        cv.notify_one();                           // 在锁【外】通知（推荐）
        /*
        notify_one函数的作用是唤醒一个正在等待条件变量的线程，使其获得锁并继续执行。
        它只会唤醒一个等待的线程，
        如果有多个线程在等待，那么只有一个线程会被唤醒，其他线程仍然处于等待状态。
        */

        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 模拟耗时
    }

    // 生产完毕：先设 done，再通知
    {
        std::lock_guard<std::mutex> lk(mtx);
        done = true;
        std::cout << "[producer] done = true\n";
    }
    cv.notify_all();   // 通知所有等待者（可能有多个消费者，且这是终局信号）
}
/*
AI 解释：

生产者：锁内改状态，锁外 notify
为什么 notify 放在锁外？
问题：notify_one 会唤醒一个消费者，但消费者被唤醒后第一件事是重新 lock() —— 
而锁还在你手里（你没出作用域），所以消费者会立即又阻塞，然后你才解锁、它才真正拿到锁。
白白浪费一次上下文切换。标准推荐：改状态在锁内，notify 在锁外。
功能上锁内锁外都对（不会错），只是锁外性能更好。
但也有反例：有些场景需要"原子地改状态 + 通知"以避免 lost wakeup

生产者最后的 done：为什么在锁内设，锁外 notify
done 必须在锁内改：它是和 q 一起被判断的，必须和 q 的修改是原子的整体。
如果 done 不加锁改，
消费者可能看到"q 空 + done 旧值 false" → 继续等 → 永久挂起（因为 done 已经不会再被设了）。
notify_all 而不是 notify_one：因为有多个消费者时（本例只有 1 个，但模式要通用），
done 是全局终局信号，必须通知所有人，否则某些消费者会永远等下去

另一解释：
done 必须和 q 一样受 mtx 保护。
原因：消费者"检查 pred + 进入 wait"这件事，必须和生产者"改 done + notify"互斥。
否则：生产者可能在"消费者检查完、还没真正挂起"的窗口改 done 
并 notify → notify 丢失 → done 已经是 true，生产者不会再 notify → 消费者永久挂起。
锁的作用：让"改状态+通知" 和 "检查状态+挂起" 成为互斥的整体，堵死这个窗口
*/

/*
AI 解析 wait:
cv.wait(lk, pred) 内部到底怎么工作
最基础的 cv.wait(lk)（无谓词版）
void wait(std::unique_lock<std::mutex>& lk) {
    // ① 此刻必须持有 lk
    // ② 原子地做：unlock(lk) + 把线程挂到 cv 等待队列 + 进入 sleep
    // ③ 被 notify 唤醒后：重新 lock(lk)
    // ④ 返回（注意：返回时一定持有 lk）
}

原子性 1："unlock + 挂起" 是原子的
如果 unlock 和挂起之间能被其他线程插队，就会产生lost wakeup 窗口：
消费者: unlock(lk)
        ← 如果这里能插入生产者：done=true + notify_all
        ← notify 找不到消费者（消费者还没挂起）→ 通知丢失
消费者: 挂起  → 睡死

原子性 2："被唤醒后重新加锁" 是必然的
cv.wait(lk) 返回时，lk 一定持有锁。这是标准保证的。
因为 wait 返回后你要继续检查条件（q.empty()、done），检查必须在锁里做。
wait 帮你在返回前就重新锁上。

cv.wait(lk, pred) = while + wait(lk) 的封装
wait(lk, pred) 不是"睡一次"，是"睡到 pred 为真"。
每次醒来都必须重新检查 pred——因为醒来 ≠ 条件满足（虚假唤醒 / 错误唤醒）。
只要 pred 为 false，就继续睡。
只有 pred 为 true 才返回，返回时持有锁。
*/


// ---------------- 消费者 ----------------
void consumer() {
    while (true) {
        std::unique_lock<std::mutex> lk(mtx);      // ① 进入临界区
        // ② 带 predicate 的 wait —— 这是本文件最关键的一行
        cv.wait(lk, [] { return !q.empty() || done; });

        // ③ 被唤醒后：判断是不是"该收工了"
        if (q.empty() && done) {
            std::cout << "[consumer] queue empty and done, exit\n";
            break;                                  // lk 析构自动解锁
        }

        // ④ 取一个元素
        int val = q.front();
        q.pop();
        lk.unlock();                                // ⑤ 提前解锁：后面处理不需要持锁

        std::cout << "[consumer] pop  " << val << "  (processing...)\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 模拟处理
        // lk 析构时如果已 unlock，不会再解锁（unique_lock 有 owns_lock_ 标记）
    }
}



int main() {
    std::thread p(producer);
    std::thread c(consumer);
    p.join();
    c.join();
    std::cout << "[main] all done\n";
    return 0;
}
/*
[producer] push 1
[consumer] pop  1  (processing...)
[producer] push 2
[consumer] pop  2  (processing...)
[producer] push 3
[consumer] pop  3  (processing...)
[producer] push 4
[consumer] pop  4  (processing...)
[producer] push 5
[consumer] pop  5  (processing...)
[producer] push 6
[consumer] pop  6  (processing...)
[producer] push 7
[consumer] pop  7  (processing...)
[producer] push 8
[consumer] pop  8  (processing...)
[producer] push 9
[consumer] pop  9  (processing...)
[producer] push 10
[consumer] pop  10  (processing...)
[producer] done = true
[consumer] queue empty and done, exit
[main] all done
*/