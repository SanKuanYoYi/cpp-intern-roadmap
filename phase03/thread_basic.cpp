// 演示: 多线程启动 / get_id / join / detach / sleep_for
#include <thread>
#include <iostream>
#include <chrono>
#include <vector>
#include <sstream>
#include <iomanip>

// 当前时间戳 (HH:MM:SS.mmm)
//static 加在全局函数前只有一个作用：
//限制这个函数只能在本文件（.cpp）里被调用，别的 .cpp 文件看不到它

/*
真正的时间戳 (timestamp)	从 1970-01-01 00:00:00 UTC 到现在经过的秒数/毫秒数	1697123456
格式化的时间字符串	         人类可读的时间	                                2024-10-12 14:30:56.123
程序内部几乎所有时间运算，用的都是第一种（一个整数，好算），打印给人看才转成第二种。
now_str 干的就是：取内部时间（整数）→ 转成人类能看懂的字符串。
*/

static std::string now_str() {
    auto now = std::chrono::system_clock::now();            // ①
    auto t   = std::chrono::system_clock::to_time_t(now);   // ②
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch()) % 1000;          // ③
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);   // Windows 专用
#else
    localtime_r(&t, &tm_buf);   // Linux/Mac 专用
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}
/*
第一次接触std::chrono
AI 解析：

① auto now = std::chrono::system_clock::now();
现在几点？ 从系统时钟取当前时刻，存进 now。
std::chrono = C++11 引入的"时间库"，专门处理时间。
system_clock = "墙上的钟"，也就是我们日常说的"现在 14:30"。
now() = 取当前瞬间。
now 是一个"时间点对象"（time_point），不是给人看的东西，是个内部的数。
打个比方：now 就像银行系统里记录的一笔交易的精确账本编号 —— 精确、唯一，但不是给人阅读的。

② auto t = std::chrono::system_clock::to_time_t(now);
把上面那个"时间点对象"转成老式 C 时间（time_t，本质是一个从 1970 年起算的整数秒）。
为什么要转？因为后面步骤 ④ 用的 localtime_* 函数是C 语言遗留的老函数，只认 time_t。
这是新库和老库交接处的"翻译"。
类比：你现在的交易记录格式是新系统（chrono），但后面要拿去跟老会计（C 函数）对账，
得先换成老会计认得的老格式（time_t）。

③ 取"毫秒"部分
auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
              now.time_since_epoch())   // 从 1970 到现在共多少毫秒
          % 1000;                       // 对 1000 取余 → 剩下"不够 1 秒"的那部分毫秒

now.time_since_epoch() = 从"1970 年起点"到现在经过的总时长。
duration_cast<milliseconds>(...) = 把这个时长换算成毫秒（一个整数）。
% 1000 = 除以 1000 取余数。
为什么要取余？因为秒的部分第 ④ 步会处理（给出 14:30:56），这里只需要"还剩多少毫秒"。
举例：总共经过了 12345678 毫秒 = 12345 秒 678 毫秒。
12345678 % 1000 = 678 → 就是这 678ms，会拼到 14:30:56.678 的后三位。
类比：你要报"3 分 42 秒 500 毫秒"，③ 就是算出"500 毫秒"那部分。

④ localtime_s / localtime_r —— 这堆宏到底在干什么？
std::tm tm_buf;
localtime_s(&tm_buf, &t);   // Windows
localtime_r(&t, &tm_buf);   // Linux / macOS
目标：把"从 1970 年起的整数秒" 拆成"年、月、日、时、分、秒"结构体（std::tm）。
tm 是一个 C 结构体，里面有一堆字段：tm_year、tm_mon、tm_mday、tm_hour、tm_min、tm_sec……

为什么有两个函数？ 因为线程安全的老 API 有两种版本，各平台不一样：
平台	函数	参数顺序
Windows (MSVC)	localtime_s(&tm, &t)	先给结果 buf，再给时间
Linux / macOS	localtime_r(&t, &tm)	先给时间，再给结果 buf
不仅名字不同，参数顺序都是反的！ 这就是为什么不能一行写死。
还有一个上古的 localtime(&t)，它返回一个静态指针 —— 多线程同时调用会互相覆盖，所以不用它

宏 #ifdef / #else / #endif：
这是编译期开关，不是运行期判断。翻译成人话：

"如果当前编译器定义了 _WIN32 这个符号（说明在 Windows 上编译），就用上面那行；否则用下面那行。"
#ifdef X = if "符号 X 被定义过"     #else = 否则        #endif = 结束
MSVC（Windows 编译器）自动定义了 _WIN32，Linux 的 gcc/clang 不定义。
所以这一小段代码同时能在 Windows 和 Linux 上编译通过。

预处理指令，在真正编译前由预处理器处理 —— 结果是：
Windows 编译后只剩 localtime_s 那一行，Linux 只剩 localtime_r 那一行。相当于给编译器发了个"选择指令"。

类比：就像你去餐厅点菜，菜单写着"如果是工作日，上 A 套餐；否则上 B 套餐"。
厨师（编译器）只看你今天是不是工作日，然后只做那一份，另一份根本不会出现。

⑤ 拼接字符串
std::ostringstream oss;
oss << std::put_time(&tm_buf, "%H:%M:%S")   // 把 tm 里的时分秒格式化
    << '.'
    << std::setfill('0') << std::setw(3) << ms.count();  // 毫秒补零到 3 位
return oss.str();

td::ostringstream = "往字符串里 << 东西"的工具。你会用 cout << ... 往屏幕输出，
ostringstream 就是往内存里的字符串输出，用法完全一样，最后 oss.str() 取出来。
类比：cout 是"喊话"，ostringstream 是"记到纸上"。
std::put_time(&tm_buf, "%H:%M:%S") = 把 tm 结构体按格式打印成 "14:30:56"

宽度不够 3 位时，左边补 0。
所以 ms.count() 如果是 7，会输出 "007"；如果是 456，输出 "456"。
为什么需要：不补零的话 14:30:56.7 会被人误读成 7 毫秒还是 70 毫秒（实际是 7ms）。
补零成 .007 一看就懂。
最终拼出来的字符串："14:30:56.007"。
*/



// ---- 可 join 的任务：主线程会等它 ----
void work_joined(int id) {
    std::cout << "[join  ] start  work_id=" << id
              << " thread_id=" << std::this_thread::get_id()
              << " time=" << now_str() << "\n";

    // 睡一会儿，观察并发效果
    std::this_thread::sleep_for(std::chrono::milliseconds(100 * id));

    std::cout << "[join  ] finish work_id=" << id
              << " thread_id=" << std::this_thread::get_id()
              << " time=" << now_str() << "\n";
}

// ---- 被 detach 的任务：主线程不管它，走自己的 ----
void work_detached(int id) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50 * id));
    std::cout << "[detach] work_id=" << id
              << " thread_id=" << std::this_thread::get_id()
              << " time=" << now_str() << "\n";
}

/*
AI 解析 join、detach：

假设你是主线程（main），你雇了 4 个外卖员（4 个子线程）去送餐。你的程序有个"下班"（main return）的规则。
t.join() = "我等他送完再下班"
std::thread t1(送外卖, 1);
t1.join();    // 主线程在这里"停住"，等到 t1 送完餐才继续往下走
主线程会原地等，外卖员不回来，主线程不走下一步。
每个外卖员都送完，主线程才继续、最后才下班。
有秩序、可控、能拿到结果，但主线程被卡住。

t.detach() = "我把他辞退了，交给社会管，我先走"
std::thread t1(送外卖, 1);
t1.detach();   // 主线程立刻走下一步，不等
主线程不等，立刻做自己的事。
这个外卖员变成"自由职业者"（后台线程），跟"你"再也没关系。
问题来了：如果你（主线程）很快就下班了、餐馆（进程）关门了，
那外卖员还在路上——餐送不出去，人也被强制消失。
所以用 detach 后通常要让主线程多待一会儿（示例里 sleep_for(600ms)），
否则后台线程还没打印东西，进程就结束了。
给后台线程留点时间，别让 main 跑得太快把整个进程关了。

为什么 std::thread 对象析构时必须二选一？
回到外卖比喻：
一个 std::thread 对象 = 你手上的一张雇佣合同纸。
合同可以有两种结局：
join() = 合同上写"我承认我等他"     detach() = 合同上写"我不管他了"
如果你既没写"等他"也没写"不管"，这张纸突然被撕掉（对象析构）了 —— 那个外卖员算谁的？
他还在路上，你也不知道要不要负责。
C++ 的做法很硬核：撕掉这张"意义不明"的合同，
直接 std::terminate() 让你程序崩掉，逼你说清楚"到底是等，还是不等"。
为什么不默认 join？ 那程序会在很多莫名其妙的地方卡死（你不想等的地方被强制等）。
为什么不默认 detach？ 那线程可能访问已经被销毁的对象，产生看不见的 bug（比崩更可怕）。
所以标准委员会选择："你不说清楚，我就不让你通过，直接崩给你看"。
这就是"析构时没 join/detach 会 terminate" 的完整来由。
*/


/*
C++ 标准规定的"三条铁律"

规定 1：joinable() 只有两种情况为 false
标准说，一个 std::thread 对象在两种情况下 joinable() == false：
默认构造的（没有绑定任何线程）：std::thread t; → t.joinable() == false
已经被 join() / detach() / 被 move 走：处理完之后就 false 了
其余情况（正绑着一个线程）都是 true。

规定 2：join() 和 detach() 的前置条件
调用 join() 或 detach() 时，如果 joinable() == false，抛异常
如果 join() 时调用者就是被 join 的那个线程自己（自己 join 自己），抛异常
std::thread t;           // 默认构造
t.join();                // ❌ 抛 system_error（joinable == false）
或者
std::thread t2([]{ ... });
t2.join();               // ✅ ok
t2.join();               // ❌ 第二次 join，joinable 已是 false，抛异常

规定 3（最关键）：析构时 joinable() 就 std::terminate()
如果 joinable() 为 true，析构函数调用 std::terminate()。
注意它说的不是"崩"，是 std::terminate() —— 这是标准库的一个"最终兜底函数"，
默认行为就是 std::abort()（直接杀进程）。
你可以通过 std::set_terminate() 换成别的行为，但默认就是崩。

std::thread 里到底存了什么？
class thread {
    // 精简示意，非真实代码
    id              _M_id;          // 线程标识（用来判 joinable）
    pthread_t       _M_handle;      // 底层操作系统的线程句柄
    // ...
};
_M_id：std::thread::id，一个轻量的标识（本质是个整数/指针）。
默认构造的 thread，_M_id 是"空 id"。
_M_handle：pthread_t，是操作系统给的真实线程身份。pthread 才是真正干活的东西。
joinable() 的实现大约就是：
只要我这个 _M_id 不是"空 id"，就说明我绑着一个真线程 → joinable() == true。

线程是怎么"造"出来的（构造函数）    std::thread t1(work, 1);
thread::thread(F f, Args... args) {
    // 1. 把 f 和 args 打包，用 std::invoke 包一层入口函数
    // 2. 调 pthread_create 真造一个 OS 线程
    int rc = pthread_create(&_M_handle, nullptr, 入口函数, 打包数据);
    if (rc != 0) throw std::system_error(...);
    _M_id = _M_handle;   // 记下身份，标记为 joinable
}
pthread_create 返回 0 表示成功，这时操作系统里就多了一个真线程在跑。
从这一刻起：有一个真的 OS 线程在运行；
你的 std::thread 对象 t1 只是这张"雇佣合同"；
t1.joinable() == true。
注意时序：构造函数返回时，线程可能已经在跑、可能还没开始跑，你没任何保证。
这就是为什么两个线程打印顺序不固定。
*/



/*
t.join()：主线程阻塞，直到 t 对应线程跑完；t.joinable() 变为 false。线程结束后资源被回收。
t.detach()：主线程立刻返回，t 对应的线程变成"后台线程"（守护线程），
生命周期与 std::thread 对象解绑；t.joinable() 也变为 false。不能再对它调用 join()。
若 std::thread 对象析构时仍 joinable()（既未 join 也未 detach），会调用 std::terminate()。
*/















int main() {
    // ============ 场景 1：4 个线程，使用 join ============
    std::cout << "=== join 示例 ===\n";
    std::vector<std::thread> joined;
    for (int i = 1; i <= 4; ++i) {
        joined.emplace_back(work_joined, i);
    }
    for (auto& t : joined) {
        if (t.joinable()) t.join();   // 主线程阻塞等待每个子线程结束
    }
    std::cout << "所有 join 线程已结束\n\n";

    // ============ 场景 2：4 个线程，使用 detach ============
    std::cout << "=== detach 示例 ===\n";
    for (int i = 1; i <= 4; ++i) {
        std::thread t(work_detached, i);
        t.detach();                   // 脱离主线程管理，变成后台线程
    }

    // 主线程继续做自己的事（不等待子线程）
    std::cout << "[main  ] 主线程继续执行...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    std::cout << "[main  ] 主线程退出\n";
    return 0;
}