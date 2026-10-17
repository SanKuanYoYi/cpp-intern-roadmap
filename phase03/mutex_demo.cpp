// 覆盖: mutex / lock_guard / unique_lock / std::lock / scoped_lock
#include <thread>
#include <mutex>
#include <iostream>
#include <vector>
#include <chrono>

// ---------- 1. std::mutex 手动 lock/unlock ----------
static void demo_basic_mutex()
{
    std::mutex m;
    int x = 0;
    auto f = [&]
    {
        for (int i = 0; i < 100000; ++i)
        {
            m.lock();
            ++x;
            m.unlock();
        }
    };
    std::thread a(f), b(f);
    a.join();
    b.join();
    std::cout << "[1 basic  mutex    ] x=" << x << "\n";
}

// ---------- 2. lock_guard (RAII) ----------
static void demo_lock_guard()
{
    std::mutex m;
    int x = 0;
    auto f = [&]
    {
        for (int i = 0; i < 100000; ++i)
        {
            std::lock_guard<std::mutex> lk(m); // 构造锁, 析构解锁
            ++x;
        }
    };
    std::thread a(f), b(f);
    a.join();
    b.join();
    std::cout << "[2 lock_guard      ] x=" << x << "\n";
}

// ---------- 3. unique_lock 手动 unlock ----------
static void demo_unique_lock()
{
    std::mutex m;
    int x = 0;
    auto f = [&]
    {
        for (int i = 0; i < 100000; ++i)
        {
            std::unique_lock<std::mutex> lk(m);
            ++x;
            lk.unlock(); // 可在作用域内提前解锁
            // ... 这里可以做一段不持锁的耗时工作 ...
        }
    };
    std::thread a(f), b(f);
    a.join();
    b.join();
    std::cout << "[3 unique_lock     ] x=" << x << "\n";
}
/*
AI 拓展解析：
unique_lock 比 lock_guard 灵活

灵活性 1：手动 unlock() / lock()
unique_lock 自带 unlock() 和 lock() 方法，随时可以调：
std::unique_lock<std::mutex> lk(m);
++x;
lk.unlock();             // 提前解锁 ✅
do_expensive_io();       // 不持锁做耗时工作
lk.lock();               // 需要时再加锁 ✅
++x;
// 出了作用域，lk 析构时如果持有锁会自动解；如果已经解了就什么都不做
unique_lock 内部有个 owns_lock_ 布尔标记：
加锁时置 true；解锁时置 false
析构时只 if (owns_lock_) unlock() —— 保证不会重复解锁。
这就是 lock_guard 做不到的：lock_guard 没有 unlock() 方法，一持有就得持有到作用域结束

灵活性 2：延迟加锁（std::defer_lock）
std::unique_lock<std::mutex> lk(m, std::defer_lock);   // 构造时【不】加锁
// ... 这里还没锁 ...
lk.lock();                                             // 需要时自己锁
主要是配合 std::lock 同时锁多把。
defer_lock 是 tag（标签），一个字面量对象，用来告诉构造函数"我只想接管，先别锁"

灵活性 3：尝试加锁（std::try_to_lock）
std::unique_lock<std::mutex> lk(m, std::try_to_lock);   // 尝试锁，不阻塞
if (lk.owns_lock()) {
    // 成功拿到了锁
} else {
    // 没拿到，做别的去（不阻塞）
}
避免长时间等待的场景。

灵活性 4：所有权转移（move）
std::unique_lock<std::mutex> lk1(m);
std::unique_lock<std::mutex> lk2 = std::move(lk1);   // 锁的"所有权"转移到 lk2
// lk1 现在不持有锁了，lk2 持有

灵活性 5：配合条件变量
std::condition_variable cv;
std::unique_lock<std::mutex> lk(m);
cv.wait(lk, []{ return ready; });   // ← cv.wait 要求 unique_lock
//cv.wait() 必须在内部"先解锁、等通知、被叫醒后再加锁"，这个操作只有 unique_lock 能配合

unique_lock 的代价
unique_lock 比 lock_guard 多一个 owns_lock_ 布尔标记 + 多一层分支判断，开销略大
（虽然微小，但在"每秒锁几十万次"的场景下可能被察觉）
能用 lock_guard / scoped_lock 就不上 unique_lock；
只有需要"手动 unlock / defer_lock / 条件变量 / move"时才用 unique_lock。
*/

// ---------- 4. std::lock 同时锁多个 mutex ----------
// 两个线程以相反顺序请求同一对锁, 用 std::lock 避免死锁
static void demo_std_lock()
{
    std::mutex m1, m2;
    int v1 = 0, v2 = 0;
    auto f1 = [&]
    {
        for (int i = 0; i < 1000; ++i)
        {
            std::lock(m1, m2);                                   // 原子地同时锁两个, 内部采用避免死锁的算法。// ① 两把锁都锁上
            std::lock_guard<std::mutex> l1(m1, std::adopt_lock); // ② 只接管，不再 lock
            std::lock_guard<std::mutex> l2(m2, std::adopt_lock); // ③ 只接管，不再 lock
            // std::lock(m1, m2);
            // std::lock_guard<std::mutex> l1(m1);   // ❌ 死锁自己
            // std::lock_guard<std::mutex> l2(m2);   // 根本走不到这
            ++v1;
            ++v2;
        }
    };
    auto f2 = [&]
    {
        for (int i = 0; i < 1000; ++i)
        {
            std::lock(m2, m1); // 顺序相反也没问题
            std::lock_guard<std::mutex> l1(m1, std::adopt_lock);
            std::lock_guard<std::mutex> l2(m2, std::adopt_lock);
            // 同上
            ++v1;
            ++v2;
        }
    };
    std::thread a(f1), b(f2);
    a.join();
    b.join();
    std::cout << "[4 std::lock       ] v1=" << v1 << " v2=" << v2 << "\n";
}

/*
AI 解析 多把锁：

为什么"顺序不一致"会死锁？
// 线程 A
m1.lock();    // A 拿到 m1
m2.lock();    // A 想拿 m2 → 被 B 占了 → 等
              // ↓
// 线程 B
m2.lock();    // B 拿到 m2
m1.lock();    // B 想拿 m1 → 被 A 占了 → 等
// A 等 B 放 m2，B 等 A 放 m1 → 死锁
*/
/*
避免死锁
破坏哪条	方法
互斥	无锁数据结构（性能极差，慎用）
占有并等待	一次性锁全部（std::lock 就是干这个的！）
不可剥夺	用 try_lock 超时后释放重试
循环等待	所有线程按固定顺序加锁（比如永远先锁地址小的）

std::lock(a, b, c, ...) 的语义：
"要么把所有锁全拿到，要么一把都不拿。"
不会出现"拿了一半在等剩下"的中间状态。
这个性质直接破坏了"占有并等待" —— 中间状态不存在，所以死锁不可能发生。

std::lock 内部怎么做到"不会死锁"？
对每个锁按某个内部顺序 try_lock：
  - 如果成功 → 记下已拿到
  - 如果失败 → 把所有【已拿到的锁全部 unlock】，然后重新开始（回到第一步）
直到所有锁全部拿到

为什么要 std::adopt_lock

*/

// ---------- 5. scoped_lock (C++17) ----------
static void demo_scoped_lock()
{
    std::mutex m1, m2;
    int v1 = 0, v2 = 0;
    auto f1 = [&]
    {
        for (int i = 0; i < 1000; ++i)
        {
            std::scoped_lock lk(m1, m2); // 一步搞定, 参数顺序无所谓
            ++v1;
            ++v2;
        }
    };
    auto f2 = [&]
    {
        for (int i = 0; i < 1000; ++i)
        {
            std::scoped_lock lk(m2, m1);
            ++v1;
            ++v2;
        }
    };
    std::thread a(f1), b(f2);
    a.join();
    b.join();
    std::cout << "[5 scoped_lock     ] v1=" << v1 << " v2=" << v2 << "\n";
}
/*
std::scoped_lock = std::lock 的 RAII 版本。它内部就是先调 std::lock，再在析构时全部 unlock。
所以它同时做了三件事：
一次性锁全部（不死锁）；
RAII 自动解锁（不用 adopt_lock）；
无需手动调 std::lock（一行搞定）。

// C++11：三行
std::lock(m1, m2);
std::lock_guard<std::mutex> l1(m1, std::adopt_lock);
std::lock_guard<std::mutex> l2(m2, std::adopt_lock);
// C++17：一行
std::scoped_lock lk(m1, m2);
//结论：C++17 及以上，多锁场景一律用 std::scoped_lock
*/

// ---------- 6. 转账场景：死锁 vs scoped_lock 修复 ----------
struct Account
{
    std::mutex mtx;
    int balance;
    explicit Account(int b) : balance(b) {}
};

// 错误示范：加锁顺序不一致 -> 可能死锁
[[maybe_unused]] static void transfer_bad(Account &from, Account &to, int amt)
{
    std::lock_guard<std::mutex> l1(from.mtx);
    std::this_thread::sleep_for(std::chrono::milliseconds(1)); // 放大竞争窗口
    std::lock_guard<std::mutex> l2(to.mtx);
    from.balance -= amt;
    to.balance += amt;
}
//[[maybe_unused]] 是告诉编译器："这个东西我故意没用，别报警告。" c++17
//[[...]] 是 C++11 引入的"属性（attribute）"语法，用来给编译器"贴标签/下指令"
/*
transfer_bad 版本（会卡住，用 Ctrl+C 强退）
*/

// 正确做法：scoped_lock 一次性锁两把
static void transfer_good(Account &from, Account &to, int amt)
{
    std::scoped_lock lk(from.mtx, to.mtx);
    from.balance -= amt;
    to.balance += amt;
}

static void demo_transfer()
{
    // --- 死锁演示（默认注释, 打开会卡住程序）---
    // Account a(100), b(100);
    // std::thread t1(transfer_bad, std::ref(a), std::ref(b), 10);
    // std::thread t2(transfer_bad, std::ref(b), std::ref(a), 10);
    // t1.join(); t2.join();   // <-- 这里会永久挂起

    // --- 修复版 ---
    Account a(100), b(100);
    std::thread t1(transfer_good, std::ref(a), std::ref(b), 10);
    std::thread t2(transfer_good, std::ref(b), std::ref(a), 20);
    //std::ref(a) 使线程拿到 a 的引用
    t1.join();
    t2.join();
    std::cout << "[6 transfer        ] a=" << a.balance
              << " b=" << b.balance
              << " (sum=" << a.balance + b.balance << ")\n";
}

/*
AI 提示：除了 scoped_lock，还有固定顺序（按地址比较）
void transfer_ordered(Account& from, Account& to, int amt) {
    // 总是先锁"地址小"的那个，所有线程统一顺序 → 破坏循环等待
    std::mutex* first  = &from.mtx;
    std::mutex* second = &to.mtx;
    if (first > second) std::swap(first, second);
    std::lock_guard<std::mutex> l1(*first);
    std::lock_guard<std::mutex> l2(*second);
    from.balance -= amt;
    to.balance   += amt;
}
缺点：写起来烦，且如果锁的集合是动态的（比如转账要锁 N 个账户），代码会变得很复杂
*/

int main()
{
    demo_basic_mutex();
    demo_lock_guard();
    demo_unique_lock();
    demo_std_lock();
    demo_scoped_lock();
    demo_transfer();
    return 0;
}

/*
[1 basic  mutex    ] x=200000
[2 lock_guard      ] x=200000
[3 unique_lock     ] x=200000
[4 std::lock       ] v1=2000 v2=2000
[5 scoped_lock     ] v1=2000 v2=2000
[6 transfer        ] a=110 b=90 (sum=200)
*/