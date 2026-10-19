#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <string>
#include <stdexcept>
#include <cstdint> //std::uintmax_t 当前平台所支持的最大宽度的无符号整数类型
#include <iomanip>

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

// 单线程复制 4MB 循环缓冲读写
void copy_single_thread(const std::string &src, const std::string &dst)
{
    std::ifstream in(src, std::ios::binary);
    if (!in)
        throw std::runtime_error("无法打开源文件：" + src);

    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("无法打开目标文件: " + dst);

    constexpr std::size_t BUF_SIZE = 4 * 1024 * 1024; // 4MB
    std::vector<char> buf(BUF_SIZE);

    while (in)
    {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size())); // buf 是目的地
        const std::streamsize got = in.gcount();                       // 最后一次可能不足 4MB
        // in.read(指针, 大小)：从流中无格式读取指定大小的字节，存入指针指向的内存
        // xx.data()：获取容器（如 std::vector<char> 或 std::string）的底层首地址
        // gcount() (get count)：返回上一次未格式化输入操作（如 read、get、getline 等）实际读取的字符数
        /*
        为什么需要它？
        假设你分配了 1024 字节的缓冲区，调用 read 想读 1024 字节。
        但如果文件只剩下 100 字节了，read 就会在读取 100 字节后停止。
        如果你直接用 buf.size() 当作数据长度，就会多读取了 924 字节的无效垃圾数据。
        只有 gcount() 才能准确告诉你：“我实际只读到了 100 字节”。
        */
        if (got > 0)
        {
            out.write(buf.data(), got); // buf 是来源
            if (!out)
                throw std::runtime_error("写入目标文件失败");
        }
        // ostream::write 失败时默认不会抛异常，它只是把流置成 failbit | badbit
    }
    /*
    while (in) 的退出，靠的是流状态的改变。当 in 从“健康”变成“不健康”时，条件为 false，循环就退出了。
    */

    // 正常退出循环 = 读到 EOF。bad() 才代表 I/O 层严重错误
    if (in.bad())
        throw std::runtime_error("读取源文件时发生 I/O 错误");
    // 资源由 RAII 在函数返回时自动关闭
}

int main(int argc, char *argv[])
{
    const std::string src = (argc > 1) ? argv[1] : "test.bin";
    const std::string dst = (argc > 2) ? argv[2] : "out.bin";
    // 没给命令行参数时用默认值

    try
    {
        const auto sz = file_size(src);
        const double mib = static_cast<double>(sz) / (1024.0 * 1024.0);

        std::cout << "源文件: " << src << " (" << sz << " 字节, "
                  << std::fixed << std::setprecision(2) << mib << " MiB)\n";

        const auto t0 = std::chrono::high_resolution_clock::now();
        copy_single_thread(src, dst);
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

/*
# 生成 100MB 随机数据
dd if=/dev/urandom of=test.bin bs=1M count=100
// dd：Linux 下的底层数据复制/转换工具（Data Duplicator）。
// if=/dev/urandom：if 代表 Input File（输入文件）。
// /dev/urandom 是 Linux 的非阻塞随机数生成器。它会源源不断地吐出随机的二进制字节。
//（注：还有个 /dev/random，但它有时会卡住阻塞，urandom 不会卡，速度极快，适合造大文件。）
//of=test.bin：of 代表 Output File（输出文件）。把读到的随机数据写进当前目录下的 test.bin 里。
//bs=1M：bs 代表 Block Size（块大小）。每次读写 1MB 的数据。
//count=100：复制的块数量。读 100 次，每次 1MB。
执行 dd 之前，一定要反复确认 of= 后面的路径是个普通文件，绝对不要乱指向硬盘设备！

# 编译（O2 必开，标准激进优化）
g++ -O2 -std=c++17 -o copy_single copy_single.cpp

# 运行
./copy_single test.bin out.bin
//源文件: test.bin (104857600 字节, 100.00 MiB)
//复制完成 -> out.bin
//耗时: 66.57 ms  (0.067 s)
//吞吐: 1502.08 MiB/s

# 验收：无输出 = 内容完全一致
cmp test.bin out.bin && echo "✓ 内容一致"
ls -l test.bin out.bin
*/