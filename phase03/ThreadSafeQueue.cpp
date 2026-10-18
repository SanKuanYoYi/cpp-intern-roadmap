#include <condition_variable>
#include <mutex>
#include <queue>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

// ============================================================
// ThreadSafeQueue<T>
// ============================================================
/*
一个"能同时被多个线程安全用的 std::queue"。
普通 std::queue 有个致命问题：多线程同时用它就会出错。
两个线程同时改 q 的内部指针 → 数据竞争 → 程序崩溃或数据错乱。
ThreadSafeQueue 就是把 std::queue 包一层，加一把锁 + 一个条件变量，让"多个线程同时用"变安全。
*/
template <typename T>
class ThreadSafeQueue
{
private:
    /*
    q_ 是成员变量：一个 ThreadSafeQueue 对象被多个线程共用时，q_ 就是共享的。
    所以 push / try_pop / wait_and_pop / empty / size 都碰 q_，都加锁。
    */
    std::queue<T> q_;            // 共享数据（受 mtx_ 保护）
    mutable std::mutex mtx_;     // mutable: 让 const 成员函数也能加锁
    std::condition_variable cv_; // 数据变化通知
    /*
    empty() 和 size() 是 const 成员函数（调用它们不会修改对象）。
    但 std::lock_guard 的构造会调用 mtx_.lock()，
    而 lock() 是非 const 方法 —— 编译器在 const 函数里调用它，会报错。
    mutable 告诉编译器：
    "这个成员即使在 const 成员函数里也允许被修改。"
    */



public:
    // ---------- 生产者：入队 ----------
    void push(T value)
    {
        {
            std::lock_guard<std::mutex> lk(mtx_); // 临界区：只保护 push 本身
            q_.push(std::move(value));            // move 进队列，避免再次拷贝
        } // ← 解锁
        cv_.notify_one(); // 锁外 notify（避免 hurry-up-and-wait）
    }
    /*
    两处 move 都是为了"避免不必要的复制"
    如果 q_.push(value) 不用 move，value 会被复制一份进入 queue。
    用 move，直接把 value 内部的资源搬进去，省一次复制
    */
    /*
    为什么 push 签名是 T value 而不是 const T&？ 值传递 + move 进队列，
    可以让"临时对象"直接被 move 进队列，一次内存构造。
    用 const T& 的话，队列里必须有拷贝构造。这是典型的"按值 + move"惯用法。
    */



    // ---------- 消费者 A：非阻塞取 ----------
    bool try_pop(T &value)
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (q_.empty())
            return false;              // 没数据，立即返回 false
        value = std::move(q_.front()); // 先 move 出
        q_.pop();                      // 再 pop
        return true;
    }



    // ---------- 消费者 B：阻塞取 ----------
    void wait_and_pop(T &value)
    {
        std::unique_lock<std::mutex> lk(mtx_);
        // 带谓词的 wait：队列非空才返回（防虚假唤醒 / 错误唤醒 / lost wakeup）
        cv_.wait(lk, [this]
                 { return !q_.empty(); });
        value = std::move(q_.front());
        q_.pop();
        // 必须先 move 再 pop  反过来就崩
        // 离开作用域时 lk 自动 unlock（unique_lock 的 RAII）
    }
    /*
    q_ 是本类的成员变量。lambda 本身是个"外面新造的函数"，它不自动知道 this 是谁。
    必须显式 [this] 抓进来，才能在 lambda 里访问 q_
    */



    // ---------- 查询：空？ ----------
    bool empty() const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return q_.empty();
    }

    // ---------- 查询：元素个数 ----------
    std::size_t size() const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return q_.size();
    }
};

// ============================================================
// 测试：3 生产者 / 2 消费者 / 300 条数据
// ============================================================
int main()
{
    ThreadSafeQueue<int> q;

    constexpr int kProducers = 3;
    constexpr int kPerProducer = 100;
    constexpr int kTotal = kProducers * kPerProducer; // 300
    constexpr int kConsumers = 2;
    constexpr int kSentinel = -1; // 结束信号（生产者不会产生 -1）

    std::atomic<long long> sum{0}; // 消费者累加所有取到的数据
    std::atomic<int> count{0};     // 消费者统计取到多少条

    // ---------- 生产者：push 自己的 100 个唯一值 ----------
    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (int p = 0; p < kProducers; ++p)
    {
        producers.emplace_back([&q, p]
                               {
            for (int i = 1; i <= kPerProducer; ++i) {
                q.push(p * kPerProducer + i);   // p=0 → 1..100；p=1 → 101..200；p=2 → 201..300
            } });
    }
    /*
    q	&q（引用）	所有生产者要往同一个队列 q 里 push，不能各自复制一份（那就各玩各的了）
    p	p（值）	    p 是"当前是第几个生产者"，每个线程要记住自己的编号，各复制一份
    */



    // ---------- 消费者：wait_and_pop 循环，遇 sentinel 退出 ----------
    std::vector<std::thread> consumers;
    consumers.reserve(kConsumers);
    for (int c = 0; c < kConsumers; ++c)
    {
        consumers.emplace_back([&]
                               {
            while (true) {
                int v = 0;
                q.wait_and_pop(v);                 // 阻塞到拿到一个元素
                if (v == kSentinel) break;         // 收到结束信号 → 退出
                sum  .fetch_add(v, std::memory_order_relaxed);
                count.fetch_add(1, std::memory_order_relaxed);
            } });
    }
    /*
    fetch_add 是 std::atomic 的方法。
    td::atomic<> = "这个变量的所有操作都是原子的，多线程同时加不会出错"。
    fetch_add(v) = "原子地把 v 加到 sum 上"。
    常用的 fetch_add / fetch_sub 等等

    为什么不用 std::mutex 保护 sum？ 因为 atomic 更轻量：
    用 CPU 的硬件原子指令实现，没有锁的开销，不会被别的线程阻塞。
    单变量加加减减，用 atomic 比用锁好。
    
    */



    // ---------- 主线程：等生产者全完成 ----------
    for (auto &t : producers)
        t.join();

    // ---------- 生产者完成后：推 kConsumers 个 sentinel 叫醒消费者 ----------
    // 数量必须 == 消费者数：每个消费者拿一个就退出
    for (int c = 0; c < kConsumers; ++c)
    {
        q.push(kSentinel);
    }

    // ---------- 等消费者退出 ----------
    for (auto &t : consumers)
        t.join();

    // ---------- 校验 ----------
    // 1..300 之和 = 300 * 301 / 2 = 45150
    constexpr long long kExpectedSum =
        static_cast<long long>(kTotal) * (kTotal + 1) / 2;

    std::cout << "count = " << count.load()
              << "  (expected " << kTotal << ")\n";
    std::cout << "sum   = " << sum.load()
              << "  (expected " << kExpectedSum << ")\n";

    assert(count.load() == kTotal);
    assert(sum.load() == kExpectedSum);

    std::cout << "[PASS] all checks passed\n";
    return 0;
}
/*
count = 300  (expected 300)
sum   = 45150  (expected 45150)
[PASS] all checks passed
*/


/*
为什么 empty() 的返回值是"瞬时快照"
if (!q.empty()) {           // ← 读到"不空"
    int v;
    q.wait_and_pop(v);      // ← 但在这一瞬间，另一个消费者可能已经拿走了唯一的元素
}

所以 empty() / size() 的返回值只能用来"看一眼"（打印、日志、调试），不能用来"决定要不要 pop"。
想"拿数据" → 直接用 try_pop 或 wait_and_pop，它们内部会处理"有没有数据"；
不要写 if (!q.empty()) q.wait_and_pop(v);，这两步之间队列可能已经空了。
*/



/*
AI 解析、拓展：

try_pop 适用场景
需要"非阻塞保证"的线程：UI 线程、信号处理线程、某些实时任务——不能因为没数据就卡住。
周期性轮询：后台线程偶尔过来看看有没有活。
退出收尾阶段：消费者知道"生产者已完成"，循环 try_pop 把剩余数据清空后退出（配合 done 标志）。
组合使用：wait_and_pop 主循环 + 收尾时 try_pop 排空

wait_and_pop 适用场景
典型生产者-消费者：消费者专门等数据干活，没数据就睡。
事件循环 / 消息分发：网络服务、GUI 事件队列。
需要"零 CPU 占用"：阻塞在 cv 上，操作系统层面睡眠，完全不占 CPU 时间片。

一旦生产者结束，队列空了，
wait_and_pop 会永远等下去——它只检查 !q_.empty()，不检查"生产者还在不在"。
两种解决方案：

方案 A：sentinel 哨兵值
// 生产者结束 → push 特殊值 -1（T 里约定 -1 是 sentinel）
// 消费者拿到 -1 就退出
while (true) {
    int v;
    q.wait_and_pop(v);
    if (v == kSentinel) break;
    // 处理 v
}
依赖业务类型里有"特殊值"；如果 T 没有额外空间
如果多个消费者、生产者只 push 1 个 sentinel，会漏。
必须保证 sentinel 数 ≥ 消费者数，或每个消费者一个。

方案 B：给队列增加 close() 语义
*/