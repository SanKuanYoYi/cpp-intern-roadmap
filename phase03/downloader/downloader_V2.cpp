#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <string>
#include <stdexcept>
#include <iomanip>
#include <thread>
#include <exception>

// ---------- 获取文件大小（沿用 V1 ） ----------
std::uintmax_t file_size(const std::string &path)
{
    std::ifstream open(path, std::ios::binary | std::ios::ate);
    if (!open)
        throw std::runtime_error("无法打开文件、获取大小：" + path);
    const auto pos = open.tellg();
    if (pos < 0)
        throw std::runtime_error("tellg 失败：" + path);
    return static_cast<std::uintmax_t>(pos);
}

// ---------- 每个分段的描述 ----------
struct Segment
{
    std::size_t offset = 0;   // 段在源文件中的起始字节
    std::size_t length = 0;   // 段长
    std::vector<char> buffer; // 读完后的数据（主线程写时用）
    std::exception_ptr error; // 线程内捕获的异常
};
/*
AI 解析：
为什么 error 要用 std::exception_ptr
工作线程里如果直接 throw 异常，会发生什么？
如果线程函数里抛出的异常没有被捕获，程序会直接调用 std::terminate 崩溃。
即使捕获了，主线程也无法直接知道子线程出了什么错。
解决：std::exception_ptr 是一个可以跨线程传递的异常指针。
... 见后
*/

// ---------- 单个线程：读取自己那一段 ----------
// 注意：每个线程独立打开 ifstream，各用各的文件位置指针，无共享状态 → 无锁、无竞争
void read_segment(const std::string &src, Segment &seg)
{
    try
    {
        if (seg.length == 0)
            return; // 空段直接返回（N > 文件字节数时才可能）

        std::ifstream in(src, std::ios::binary);
        if (!in)
            throw std::runtime_error("无法打开源文件：" + src);

        in.seekg(static_cast<std::streamoff>(seg.offset));
        if (!in)
            throw std::runtime_error("seekg 失败, offset=" + std::to_string(seg.offset));

        seg.buffer.resize(seg.length); // 预分配，读的时候不用再动内存
        in.read(seg.buffer.data(), static_cast<std::streamsize>(seg.length));

        const auto got = in.gcount();
        if (got != static_cast<std::streamsize>(seg.length))
        {
            throw std::runtime_error(
                "读取不完整：段 " + std::to_string(seg.offset) +
                "，期望 " + std::to_string(seg.length) +
                "，实际 " + std::to_string(got));
        }
    }
    catch (...)
    {
        // 线程函数里不能直接把异常扔出去（会 std::terminate）
        // 捕获存 exception_ptr，回到主线程统一 rethrow
        seg.error = std::current_exception();
    }
}
/*
                    单线程版（步骤一）	                        多线程版（步骤二）
read 的请求长度	    固定 4MB（buf.size()）	                    seg.length（本段精确长度）
一次 read 覆盖多少	只是文件的一小块，后面还要继续读	            本段的全部字节，读完这段就结束
循环	            有，反复 read 直到 EOF	                    无，只 read 一次
gcount 的正常值	    中间块 = 4MB；尾块 < 4MB 是正常的	        必须 = length，否则就是异常

gcount() 的准确定义是：
上一次未格式化输入操作（read/get/getline 等）实际读取的字符数。
它反映的是"这次 read 到底拿到了多少字节"，不是"还剩多少没读"。
单线程版里尾块 got < 4MB，是因为文件真的只剩那么多了——read(4MB) 请求 4MB，但文件末尾不够，只能给这么多。
多线程版里 read(seg.length) 请求的是本段长度，而本段长度是精确算出来的
所以这次 read 一定能读满 seg.length，got 必须等于 length
理论上不该发生，这个检查就是防御性的
*/

/*
/*
为什么单线程版要循环、这里不用
因为目标是不同的：
单线程版：一个流顺序扫完整个文件。缓冲区定长 4MB，必然要循环；
循环的最后一次自然可能不满，所以要靠 gcount 处理尾块。
多线程版：每个线程的职责是"读我的 [offset, offset+length)"
区间是已知且精确的，一次 read(length) 就该覆盖它。
如果没读满，说明数据没按预期来——这就是错误。
*/

// ---------- 主流程：拆分 → 并行读 → 串行写 ----------
void copy_multi_thread(const std::string &src, const std::string &dst, unsigned nthreads)
{
    const std::uintmax_t size = file_size(src);

    // 空文件：直接建个空目标文件即可。防御编程
    if (size == 0)
    {
        std::ofstream(dst, std::ios::binary | std::ios::trunc);
        // 这是一条创建临时 ofstream 对象、然后立刻让它析构的语句。效果：
        // 构造时以 binary | trunc 模式打开 dst → 文件被创建（或已存在则截断为 0 字节）；
        // 语句结束，临时对象析构 → 析构函数里自动 close()；
        // 结果：磁盘上多了一个 0 字节的 dst 文件。
        /*
        等价于更常见的写法：
        {
        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
        // out 一出作用域就析构关闭
        }
        或直接：
        std::ofstream(dst, std::ios::binary | std::ios::trunc).close();
        // 显式 close 一下，可读性更好
        */
        return;
    }

    if (nthreads == 0) // 防除零
        nthreads = 1;
    if (nthreads > size)
        nthreads = static_cast<unsigned>(size); // 空线程无意义

    // ---------- 1. 切段：base + 余数，保证首尾相接、完整覆盖 ----------
    //    size = base * N + rem
    //    前 rem 段每段多 1 字节；其余段都是 base 字节
    const std::uintmax_t base = size / nthreads; // 每段至少这么多
    const std::uintmax_t rem = size % nthreads;  // 余数，分给前 rem 段

    std::vector<Segment> segs(nthreads);
    // segs[i] 给第 i 个线程专用
    // 必须是动态的 vector，不能用固定数组  nthreads 来自命令行，编译期不知道
    // vector  RAII + 连续内存
    std::uintmax_t off = 0; // 游标
    for (unsigned i = 0; i < nthreads; ++i)
    {
        const std::uintmax_t len = base + (i < rem ? 1 : 0); // 前 rem 段各多 1 字节
        segs[i].offset = static_cast<std::size_t>(off);
        segs[i].length = static_cast<std::size_t>(len);
        off += len; // 游标推进到下一段起点
        /*
        base：每段至少有这么多字节；
        rem：还剩几个字节没分掉，0 ≤ rem < nthreads；
        前 rem 段各多拿 1 字节，后面 nthreads - rem 段就是 base。
        */
    }
    /*
    也可以：
    // 前 N-1 段固定 base，最后一段兜底剩下的
    for (i = 0; i < nthreads - 1; ++i) len[i] = base;
    len[nthreads - 1] = size - base * (nthreads - 1);   // = base + rem
    也能保证总和 = size，但负载不均
    */

    // off 此时必须等于 size（可作为断言：覆盖不完整 = bug）
    if (off != size)
        throw std::logic_error("分段覆盖不完整");

    // ---------- 2. 起 N 个线程并行读 ----------
    //    每个线程只碰自己的 segs[i]，没有共享写 → 天然无锁
    std::vector<std::thread> threads;
    threads.reserve(nthreads);
    for (unsigned i = 0; i < nthreads; ++i)
    {
        threads.emplace_back(read_segment, std::cref(src), std::ref(segs[i]));
        /*
        std::thread 默认按值拷贝参数
        返回类型	线程拿到后	能否修改原对象
        std::ref(x)	reference_wrapper<T>	绑定到 T&	✅ 能
        std::cref(x)	reference_wrapper<const T>	绑定到 const T&	❌ 只能读

        //AI拓展 作为了解：
        //为什么普通函数调用不需要 ref，thread 就需要
        //thread 为了"把参数安全地存到线程对象里、稍后在新线程里调用"，必须对参数做一次按值存储（因为 Args&& 可能是右值，也可能调用时机在构造之后，不能只存引用——引用可能悬垂）。这个"按值存储"就吃掉了引用语义。
        //reference_wrapper 是标准库提供的唯一合法逃逸口：它本身按值可拷贝，但行为上仍是引用
        */
    }

    // ---------- 3. join 全部（同时合并异常） ----------
    for (auto &t : threads)
        t.join();
    for (unsigned i = 0; i < nthreads; ++i)
    {
        if (segs[i].error)
            std::rethrow_exception(segs[i].error);
    }

    // ---------- 4. 主线程依次写回（顺序写，file pointer 自然连续推进） ----------
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("无法打开目标文件: " + dst);

    for (unsigned i = 0; i < nthreads; ++i)
    {
        out.write(segs[i].buffer.data(),
                  static_cast<std::streamsize>(segs[i].length));
        if (!out)
            throw std::runtime_error("写入目标文件失败，段 " + std::to_string(i));
    }
}

int main(int argc, char *argv[])
{
    const std::string src = (argc > 1) ? argv[1] : "test.bin";
    const std::string dst = (argc > 2) ? argv[2] : "out.bin";

    // 可选第 3 个参数：线程数，默认 4

    // main 的职责是"接受用户输入并做语法级校验"（是不是数字），
    // copy_multi_thread 的职责是"做语义级可行修正"（这个数在这个文件上合理吗）
    unsigned nthreads = 4; // 默认
    if (argc > 3)
    {
        try
        {
            const unsigned long v = std::stoul(argv[3]);
            if (v == 0)
            {
                std::cerr << "线程数必须 >= 1, 收到: " << argv[3] << "\n";
                return 1;
            }
            if (v > 1024)
            { // 可选：给个上限，防止 100 万线程的荒谬输入
                std::cerr << "线程数过大 (上限 1024), 收到: " << v << "\n";
                return 1;
            }
            nthreads = static_cast<unsigned>(v);
        }
        catch (...)
        {
            std::cerr << "线程数参数无效: " << argv[3] << "\n";
            return 1;
        }
    }
    /*
    不带第 3 个参数时，argc == 3，argv[3] 根本不存在。
    如果不在 main 里给默认值，nthreads 就是个未初始化变量——UB。
    所以默认值是必须的
    */

    try
    {
        const auto sz = file_size(src);
        const double mib = static_cast<double>(sz) / (1024.0 * 1024.0);

        std::cout << "源文件: " << src << " (" << sz << " 字节, "
                  << std::fixed << std::setprecision(2) << mib << " MiB)\n";
        std::cout << "线程数: " << nthreads << "\n";

        const auto t0 = std::chrono::high_resolution_clock::now();
        copy_multi_thread(src, dst, nthreads);
        const auto t1 = std::chrono::high_resolution_clock::now();

        const std::chrono::duration<double, std::milli> ms = t1 - t0;
        const double secs = ms.count() / 1000.0;
        const double mbps = (secs > 0.0) ? (mib / secs) : 0.0;

        std::cout << "复制完成 -> " << dst << "\n";
        std::cout << "耗时: " << std::setprecision(2) << ms.count()
                  << " ms  (" << std::setprecision(3) << secs << " s)\n";
        std::cout << "吞吐: " << std::setprecision(2) << mbps << " MiB/s\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "错误: " << e.what() << "\n";
        return 1;
    }
    return 0;
}


// ./downloader_V2 test.bin out.bin
/*
源文件: test.bin (104857600 字节, 100.00 MiB)
线程数: 4
复制完成 -> out.bin
耗时: 257.14 ms  (0.257 s)
吞吐: 388.89 MiB/s
*/



// ./downloader_V2 test.bin out.bin 1
/*
源文件: test.bin (104857600 字节, 100.00 MiB)
线程数: 1
复制完成 -> out.bin
耗时: 184.26 ms  (0.184 s)
吞吐: 542.71 MiB/s
*/


// ./downloader_V2 test.bin out.bin 2
/*
源文件: test.bin (104857600 字节, 100.00 MiB)
线程数: 2
复制完成 -> out.bin
耗时: 102.46 ms  (0.102 s)
吞吐: 976.01 MiB/s
*/


// ./downloader_V2 test.bin out.bin 4
/*
源文件: test.bin (104857600 字节, 100.00 MiB)
线程数: 4
复制完成 -> out.bin
耗时: 109.47 ms  (0.109 s)
吞吐: 913.47 MiB/s
*/


///downloader_V2 test.bin out.bin 8
/*
源文件: test.bin (104857600 字节, 100.00 MiB)
线程数: 8
复制完成 -> out.bin
耗时: 121.37 ms  (0.121 s)
吞吐: 823.96 MiB/s
*/


// cmp test.bin out.bin && echo "内容一致"
/*
内容一致
*/



/*
Valgrind
==7371== HEAP SUMMARY:
==7371==     in use at exit: 0 bytes in 0 blocks
==7371==   total heap usage: 30 allocs, 30 frees, 1,185,544 bytes allocated
==7371== 
==7371== All heap blocks were freed -- no leaks are possible
==7371== 
==7371== For lists of detected and suppressed errors, rerun with: -s
==7371== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
*/

/*
helgrind
==7416== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 51 from 18)
*/






/*
AI 分析结果：
为什么"先升后降"？

1→2 线程：收益最大。单线程时 CPU 有一个核在做 memcpy + 系统调用，另一个核空闲；
2 线程时两个核同时读 + 拷贝，直接翻倍。
2→4 线程：收益递减。为什么？
内存带宽是瓶颈：100MB 全在 page cache 里，读就是纯 memcpy。
内存带宽是固定的（比如 DDR4 双通道 ~40 GB/s，但要给 4 个核分，每核实际可用带宽下降）。
线程开销：线程创建、join、ofstream 打开关闭、主线程串行写回 —— 这些不随线程数缩短，反而更多线程 = 更多 join 开销。
4→8 线程：净负收益。线程开销超过并行收益。8 个线程抢 8 个核（假设 8 核），
但主线程还要串行写，实际能干活的只有 7 个核（主线程占一个）+ 每个核要争内存带宽 → 反而更慢。
这就是经典的"多线程不是越多越好"。

默认 4 线程 257ms vs 显式 4 线程 109ms —— 预热效应
① CPU 频率爬升（Turbo Boost）
现代 CPU 默认在节能低频模式（比如 1.2 GHz）。
跑一段负载后，CPU 才"发现"有活干，逐步爬到 睿频（比如 3.5 GHz）。这个过程需要几十到几百毫秒。
② Page Cache 命中（最关键）
数据在 page cache（内存）里，但还没全部回写到磁盘；
第一次跑 copy：读 test.bin 可能和回写抢 I/O 带宽；写 out.bin 也要和回写争磁盘队列；
后续跑：test.bin 100% 在 cache，out.bin 也是新建并写入 cache（写回是异步的，不影响本次计时）；纯内存拷贝，完全不碰磁盘
③ 首次内存分配（Page Fault）


这才是性能测量的正确方法论
每个配置跑 5 次（或更多），取中位数
for n in 1 2 4 8; do
    echo "=== $n threads ==="
    for i in 1 2 3 4 5; do
        ./downloader_V2 test.bin out.bin $n | grep "耗时"
    done
done
② 跑之前先"热身"   ③ 先清缓存    等等
*/