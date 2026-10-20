/*
此版本 为 ai 整合修正的类封装建议版本，参考学习
*/

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// ============================================================
// 工具：文件大小
// ============================================================
static std::uintmax_t file_size(const std::string &path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        throw std::runtime_error("无法打开文件：" + path);
    const auto pos = in.tellg();
    if (pos < 0)
        throw std::runtime_error("tellg 失败：" + path);
    return static_cast<std::uintmax_t>(pos);
}

// ============================================================
// 单线程基线（用于对比）
// ============================================================
static void copy_single_thread(const std::string &src, const std::string &dst)
{
    std::ifstream in(src, std::ios::binary);
    if (!in)
        throw std::runtime_error("无法打开源文件：" + src);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("无法打开目标文件：" + dst);

    constexpr std::size_t BUF_SIZE = 4 * 1024 * 1024; // 4MB
    std::vector<char> buf(BUF_SIZE);

    while (in)
    {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const auto got = in.gcount(); // 尾块可能不足 4MB
        if (got > 0)
        {
            out.write(buf.data(), got);
            if (!out)
                throw std::runtime_error("写入失败");
        }
    }
    if (in.bad())
        throw std::runtime_error("读取出错");
}

// ============================================================
// Downloader
// ============================================================
class Downloader
{
public:
    Downloader(std::string src, std::string dst, unsigned nthreads)
        : src_(std::move(src)), dst_(std::move(dst)), nthreads_(nthreads) {}

    void run()
    {
        const auto size = file_size(src_);

        // 空文件：直接建空的 dst
        if (size == 0)
        {
            std::ofstream(dst_, std::ios::binary | std::ios::trunc).close();
            return;
        }

        // 边界修正
        if (nthreads_ == 0)
            nthreads_ = 1;
        if (nthreads_ > size)
            nthreads_ = static_cast<unsigned>(size);

        auto segs = plan_segments(size); // ① 算段
        completed_.store(0);             // 重置进度（成员，可重复 run）
        std::cout << "线程数: " << nthreads_ << "\n";

        read_all(segs);     // ② 并行读
        write_output(segs); // ③ 串行写
    }

private:
    std::string src_;
    std::string dst_;
    unsigned nthreads_;
    std::atomic<int> completed_{0}; // 进度：单变量计数，atomic 足够

    struct Segment
    {
        std::size_t offset = 0;
        std::size_t length = 0;
        std::vector<char> buffer;
        std::exception_ptr error; // 线程内异常传回主线程
    };

    // ---------- 1. 分段：base + 余数，保证不重不漏 ----------
    std::vector<Segment> plan_segments(std::uintmax_t size) const
    {
        const std::uintmax_t base = size / nthreads_;
        const std::uintmax_t rem = size % nthreads_; // 前 rem 段各多 1 字节

        std::vector<Segment> segs(nthreads_);
        std::uintmax_t off = 0;
        for (unsigned i = 0; i < nthreads_; ++i)
        {
            const std::uintmax_t len = base + (i < rem ? 1 : 0);
            segs[i].offset = static_cast<std::size_t>(off);
            segs[i].length = static_cast<std::size_t>(len);
            off += len;
        }
        if (off != size)
            throw std::logic_error("分段覆盖不完整");
        return segs;
    }

    // ---------- 2a. 单线程读自己那段 ----------
    void read_segment(Segment &seg)
    {
        try
        {
            std::ifstream in(src_, std::ios::binary); // ← 每线程独立 ifstream
            if (!in)
                throw std::runtime_error("无法打开源文件：" + src_);

            in.seekg(static_cast<std::streamoff>(seg.offset));
            if (!in)
                throw std::runtime_error("seekg 失败");

            seg.buffer.resize(seg.length);
            in.read(seg.buffer.data(), static_cast<std::streamsize>(seg.length));
            if (in.gcount() != static_cast<std::streamsize>(seg.length))
            {
                throw std::runtime_error("读取不完整: offset=" +
                                         std::to_string(seg.offset));
            }
        }
        catch (...)
        {
            seg.error = std::current_exception();
        }

        // 无论成功失败，都计为"完成一段"
        const int done = completed_.fetch_add(1, std::memory_order_relaxed) + 1;
        std::cout << "[progress] " << done << " / " << nthreads_
                  << " segments done\n";
    }

    // ---------- 2b. 起线程 + join + 收集异常 ----------
    void read_all(std::vector<Segment> &segs)
    {
        std::vector<std::thread> threads;
        threads.reserve(nthreads_);
        for (unsigned i = 0; i < nthreads_; ++i)
        {
            // [this, &segs, i]：segs 引用（join 前一直有效）、i 按值（关键！）
            threads.emplace_back([this, &segs, i]
                                 { read_segment(segs[i]); });
        }
        for (auto &t : threads)
            t.join();
        for (unsigned i = 0; i < nthreads_; ++i)
        {
            if (segs[i].error)
                std::rethrow_exception(segs[i].error);
        }
    }

    // ---------- 3. 串行写回 ----------
    void write_output(const std::vector<Segment> &segs) const
    {
        std::ofstream out(dst_, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("无法打开目标文件：" + dst_);
        for (unsigned i = 0; i < nthreads_; ++i)
        {
            out.write(segs[i].buffer.data(),
                      static_cast<std::streamsize>(segs[i].length));
            if (!out)
                throw std::runtime_error("写入失败，段 " + std::to_string(i));
        }
    }
};

// ============================================================
// main：跑单线程基线 + 多线程，各自计时
// ============================================================
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <src> <dst> [threads=4]\n";
        return 1;
    }
    const std::string src = argv[1];
    const std::string dst = argv[2];

    unsigned nthreads = 4;
    if (argc > 3)
    {
        try
        {
            const auto v = std::stoul(argv[3]);
            if (v == 0 || v > 1024)
                throw std::out_of_range("bad threads");
            nthreads = static_cast<unsigned>(v);
        }
        catch (...)
        {
            std::cerr << "线程数无效: " << argv[3] << "\n";
            return 1;
        }
    }

    try
    {
        const auto sz = file_size(src);
        const double mib = static_cast<double>(sz) / (1024.0 * 1024.0);
        std::cout << "源文件: " << src << " (" << sz << " 字节, "
                  << std::fixed << std::setprecision(2) << mib << " MiB)\n";

        // --- 单线程基线（写到 out_single.bin）---
        {
            const auto t0 = std::chrono::high_resolution_clock::now();
            copy_single_thread(src, "out_single.bin");
            const auto t1 = std::chrono::high_resolution_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            std::cout << "[single] " << std::setprecision(2) << ms << " ms, "
                      << std::setprecision(2) << (mib / (ms / 1000.0))
                      << " MiB/s\n";
        }

        // --- 多线程（写到用户指定的 dst）---
        {
            Downloader d(src, dst, nthreads);
            const auto t0 = std::chrono::high_resolution_clock::now();
            d.run();
            const auto t1 = std::chrono::high_resolution_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            std::cout << "[multi ] " << std::setprecision(2) << ms << " ms, "
                      << std::setprecision(2) << (mib / (ms / 1000.0))
                      << " MiB/s\n";
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "错误: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

/*
源文件: test.bin (104857600 字节, 100.00 MiB)
[single] 393.74 ms, 253.98 MiB/s
线程数: 4
[progress] 1 / 4 segments done
[progress] 2 / 4 segments done
[progress] 3 / 4 segments done
[progress] 4 / 4 segments done
[multi ] 194.61 ms, 513.84 MiB/s
*/

/*
cmp test.bin out.bin && echo "多线程 OK"
多线程 OK
*/

/*
cmp test.bin out_single.bin && echo "单线程 OK"
单线程 OK
*/