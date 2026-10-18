#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

// ============================================================
// Demo 1: 用 atomic<int> 重写 B2 的 counter
// ============================================================
namespace demo1
{

    constexpr int kThreads = 10;
    constexpr int kPerThread = 10000;
    constexpr int kExpected = kThreads * kPerThread; // 100000

    std::atomic<int> counter{0}; // ← 原子变量
                                 //  |
    void increment()
    { //  |
        for (int i = 0; i < kPerThread; ++i)
        {              //  V
            ++counter; // ← 原子 ++，等价于 counter.fetch_add(1)
        }
    }
    /*
    AI 拓展：
    std::atomic<int> 的 ++ 运算符内部：
    // 伪代码
    int atomic<int>::operator++() {
        return fetch_add(1) + 1;   // 原子地"读 + 加 + 写"三步合成一个操作
    }
    硬件保证：这条指令在执行期间，其他核心不能插进来读/写同一块内存。
    */

    int run_once()
    {
        counter.store(0); // 重置
        std::vector<std::thread> ts;
        ts.reserve(kThreads);
        for (int i = 0; i < kThreads; ++i)
            ts.emplace_back(increment);
        for (auto &t : ts)
            t.join();
        return counter.load(); // atomic 读最终值
    }

    void run()
    {
        std::cout << "=== Demo 1: atomic<int> 重写 counter ===\n";
        std::cout << "期望值: " << kExpected << "\n";
        for (int i = 1; i <= 5; ++i)
        {
            std::cout << "run " << i << ": " << run_once() << "\n";
        }
    }

} // namespace demo1

// ============================================================
// Demo 2: atomic vs mutex 性能对比
// ============================================================
namespace demo2
{

    constexpr int kThreads = 4;
    constexpr int kPerThread = 1000000; // 每线程 100 万次，总 400 万次

    // --- atomic 版本 ---
    std::atomic<long long> atomic_counter{0};

    void atomic_inc()
    {
        for (int i = 0; i < kPerThread; ++i)
        {
            ++atomic_counter;
        }
    }

    // --- mutex 版本 ---
    long long mutex_counter = 0;
    std::mutex m;

    void mutex_inc()
    {
        for (int i = 0; i < kPerThread; ++i)
        {
            std::lock_guard<std::mutex> lk(m);
            ++mutex_counter;
        }
    }

    // 通用计时模板：跑一个函数并返回毫秒
    template <typename F>
    double time_ms(F f)
    {
        // F f：参数 f 的类型是 F。这意味着你可以传入任何能像函数一样调用的东西
        auto t0 = std::chrono::high_resolution_clock::now();
        // stdhigh_resolution_clock 是 C++11 引入的 <chrono> 库中的一个高精度计时器，
        // 用于测量代码运行时间或进行高分辨率时间点的操作。
        // 它常用于性能分析、时间间隔统计等场景。 是一个类
        f();
        // 所谓的“回调”。不管传进来的是多复杂的函数，直接执行它。
        auto t1 = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
        // std::chrono::duration<double, std::milli>：这是一个模板类。
        // 第一个参数 double 是底层存储的数据类型；
        // 第二个参数 std::milli 是单位比例（表示 1/1000 秒，即毫秒）
        //.count()：从 duration 对象中提取出纯数值（double 类型）
    }

    void run()
    {
        std::cout << "\n=== Demo 2: atomic vs mutex 性能（"
                  << kThreads << " 线程 * " << kPerThread << " 次）===\n";

        // --- atomic ---
        atomic_counter.store(0);
        double t_atomic = time_ms([]
                                  {
        std::vector<std::thread> ts;
        for (int i = 0; i < kThreads; ++i) ts.emplace_back(atomic_inc);
        for (auto& t : ts) t.join(); });
        std::cout << "[atomic] result = " << atomic_counter.load()
                  << "  time = " << t_atomic << " ms\n";

        // --- mutex ---
        mutex_counter = 0;
        double t_mutex = time_ms([]
                                 {
        std::vector<std::thread> ts;
        for (int i = 0; i < kThreads; ++i) ts.emplace_back(mutex_inc);
        for (auto& t : ts) t.join(); });
        std::cout << "[mutex ] result = " << mutex_counter
                  << "  time = " << t_mutex << " ms\n";

        std::cout << "atomic 比 mutex 快约 "
                  << (t_mutex / t_atomic) << " 倍\n";
    }

} // namespace demo2
/*
为什么 atomic 比 mutex 快

mutex 做一次 ++ 的过程：
m.lock()：可能触发系统调用（如果锁被占）→ 线程进入内核态睡眠；
++counter；
m.unlock()：可能触发系统调用 → 唤醒等待的线程。
lock/unlock 本身也有开销；一旦有人抢，就是内核上下文切换（微秒级）。

atomic 做一次 ++ 的过程：
一条 lock xadd 指令 —— 用户态完成，没有内核参与，约 10~20 纳秒。
*/


// ============================================================
// Demo 3: atomic<bool> 做简单开关（跨线程启停）
// ============================================================
namespace demo3
{

    std::atomic<bool> running{true}; // 全局开关

    void worker()
    {
        long long n = 0;
        while (running.load(std::memory_order_relaxed))
        {
            ++n; // 忙循环干活（这里故意让它空转）
        }
        std::cout << "[worker] stopped after " << n << " iterations\n";
    }

    void run()
    {
        std::cout << "\n=== Demo 3: atomic<bool> 开关 ===\n";

        std::thread t(worker);

        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // 让 worker 跑 200ms
        std::cout << "[main  ] set running = false\n";
        running.store(false, std::memory_order_relaxed); // 关掉开关

        t.join();
        std::cout << "[main  ] worker joined\n";
    }

} // namespace demo3
/*
AI 拓展解析:

用途：跨线程的"启停信号"。典型场景：
后台线程死循环干活，主线程要叫它停；
用 atomic<bool> 而不是 mutex——读一个 bool 不该付锁的代价；
worker 里每轮都 load，主线程 store(false) 后，下一轮 worker 就退出。

为什么要用 atomic<bool> 而不是普通 bool？
普通 bool 有个致命问题：编译器可能把 while (running) 优化成"只读一次"：
while (running) { ++n; }   // 编译器可能认为 running 不会被改，只读一次，死循环跑到底
atomic<bool> 强制每次 load 都真的从内存读，编译器不能优化掉 → 主线程改的 false 会被 worker 看到。

load / store 的第二个参数是内存序（memory order），控制重排自由度
值	                     含义
memory_order_seq_cst	最严：所有线程看到一致的全局顺序（默认，不给参数就是这个）
memory_order_acquire	读操作专用：禁止后面的读/写重排到它前面
memory_order_release	写操作专用：禁止前面的读/写重排到它后面
memory_order_relaxed	最松：只保证"这个变量本身的读写是原子的"，不管顺序

worker 线程只关心"开关是 true 还是 false"；
没有别的数据要"发布"给另一个线程；
只要开关能从 true 变成 false 且被看到，就完事了；
*/


// ============================================================
// Demo 4: memory_order 四种顺序的简单例子
// ============================================================
namespace demo4
{

    // 经典例子：线程 A 写数据 + 设标志；线程 B 检查标志 + 读数据
    // 用 relaxed 可能读到 flag=true 但 data 还是旧值（数据竞争感）
    // 用 release/acquire 配对可保证顺序

    std::atomic<bool> flag{false};
    int data = 0;

    void writer()
    {
        data = 42; // ① 写数据
        // release：保证 ① 不会被重排到 store 之后
        flag.store(true, std::memory_order_release); // ② 发布标志
    }

    void reader()
    {
        // acquire：保证 ③ 之后的读不会重排到它之前
        while (!flag.load(std::memory_order_acquire))
        { // ③ 等标志
            std::this_thread::yield();
        }
        // ④ 此时一定看到 data == 42
        std::cout << "[demo4] reader sees data = " << data << "\n";
    }

    void run()
    {
        std::cout << "\n=== Demo 4: memory_order release/acquire ===\n";
        std::thread w(writer);
        std::thread r(reader);
        w.join();
        r.join();
    }

} // namespace demo4
/*
AI 解析：

编译器和 CPU 都会重排指令
编译器重排：
data = 42;                // 编译器看："data 没人读，写它没用" → 挪走
flag.store(true);         // 或者把这两行交换顺序
CPU 重排（乱序执行）：
现代 CPU 为了性能，不按代码顺序执行指令；
只有"有依赖"的指令才会真正按序；
两个独立的写操作（写 data、写 flag）可能被 CPU 换序执行。

结果：writer 那边实际执行顺序可能变成：
flag.store(true);      ← 先设了标志
data = 42;             ← 后写数据
然后 reader 看到：flag == true → 认为"writer 已经写完了" → 读 data
但 data 还没写！读到 0

release 加在"写"上，告诉编译器/CPU
我在这条 store 之前的所有写操作，必须在 store 真正生效之前全部完成。 不许重排到后面！

acquire 加在"读"上，告诉编译器/CPU
我在这条 load 之后的所有读操作，必须在 load 之后才执行。 不许重排到前面！

关键点：release 和 acquire 必须配对
只有 release 没有 acquire：读端可能还是重排 → 读到旧 data；
只有 acquire 没有 release：写端可能重排 → 读端看到 flag=true 但 data 是旧值；
两者配对：中间的写/读都被"墙"固定住了。

用 seq_cst（默认）这段代码也是对的。只是：
seq_cst 开销更大；
release/acquire 是最小必要保证 —— 只禁必需的，不禁多余的，更快。
*/



// ============================================================
int main()
{
    demo1::run();
    demo2::run();
    demo3::run();
    demo4::run();
    return 0;
}

/*
=== Demo 1: atomic<int> 重写 counter ===
期望值: 100000
run 1: 100000
run 2: 100000
run 3: 100000
run 4: 100000
run 5: 100000

=== Demo 2: atomic vs mutex 性能（4 线程 × 1000000 次）===
[atomic] result = 4000000  time = 40.2893 ms
[mutex ] result = 4000000  time = 133.787 ms
atomic 比 mutex 快约 3.32065 倍

=== Demo 3: atomic<bool> 开关 ===
[main  ] set running = false
[worker] stopped after 108555025 iterations
[main  ] worker joined

=== Demo 4: memory_order release/acquire ===
[demo4] reader sees data = 42
*/


//注意：atomic 不可拷贝
/*
AI 总结：
维度	std::atomic	                    std::mutex
保护范围	单个变量	                任意代码段（多变量、复杂逻辑）
实现	CPU 硬件原子指令	            操作系统 futex / 条件等待
有竞争时	忙等 / 硬件重试，           不睡眠	睡眠（不占 CPU）
无竞争开销	~10 ns	                         ~30~50 ns
有竞争开销	可能浪费 CPU（忙等）	        微秒级（内核上下文切换）
能否保护复合操作	❌ 只能保护单变量	    ✅ 任意代码
是否可重入	    无所谓（无锁）	                std::mutex 不可重入
防止数据竞争	✅ 对该变量的所有原子操作	✅ 对锁内所有代码
防止编译器优化	✅（原子操作不能被优化掉）	✅（锁是编译器屏障）
*/