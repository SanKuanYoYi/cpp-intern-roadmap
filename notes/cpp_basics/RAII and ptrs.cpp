#include <iostream>
#include <cstdio>    //std::fopen, std::fclose
#include <memory>    // std::unique_ptr
#include <stdexcept> // std::runtime_error
#include <string>

class FileGuard
{
private:
    FILE *fp;

public:
    FileGuard(const char *filename, const char *mode);
    ~FileGuard();

    FileGuard(const FileGuard &) = delete;            // 禁止拷贝构造
    FileGuard &operator=(const FileGuard &) = delete; // 禁止拷贝赋值

    FILE *get() const { return fp; } // 获取文件指针
};

class FileGuard2
{
private:
    FILE *fp_;

public:
    FileGuard2(const char *filename, const char *mode)
        : fp_(std::fopen(filename, mode))
    {
        if (!fp_)
        {
            throw std::runtime_error(std::string("Failed to open file: ") + filename);
        }
    }

    ~FileGuard2()
    {
        if (fp_)
        {
            std::fclose(fp_);
        }
    }

    FileGuard2(const FileGuard2 &) = delete;            // 禁止拷贝构造
    FileGuard2 &operator=(const FileGuard2 &) = delete; // 禁止拷贝赋值

    FILE *get() const { return fp_; } // 获取文件指针
};
/*把 FILE* 包装成一个栈对象，
利用栈对象离开作用域时自动调用析构函数的特性，实现文件的自动关闭。

为什么必须删除？
假设允许拷贝：
FileGuard f1("data.txt", "r");
FileGuard f2 = f1; // 如果没删除，编译器会生成默认拷贝构造：f2.fp_ = f1.fp_
// 此时 f1 和 f2 指向同一个 FILE*。
// f2 析构时，fclose(f2.fp_);
// f1 析构时，fclose(f1.fp_); // 二次释放，程序崩溃！

= delete 是 C++11 引入的语法，显式禁止编译器生成默认的拷贝函数
相当于把FileGuard 变成了独占所有权的类，类似于 std::unique_ptr。
*/

class FileGuard3
{
private:
    using FilePtr = std::unique_ptr<FILE, decltype(&std::fclose)>;
    FilePtr fp_;

public:
    FileGuard3(const char *filename, const char *mode)
        : fp_(std::fopen(filename, mode), &std::fclose)
    {
        if (!fp_)
        {
            throw std::runtime_error(std::string("fopen failed: ") + filename);
        }
    }

    FileGuard3(const FileGuard3 &) = delete;
    FileGuard3 &operator=(const FileGuard3 &) = delete;

    FILE *get() const { return fp_.get(); }

    FileGuard3(FileGuard3 &&) = default;
    FileGuard3 &operator=(FileGuard3 &&) = default;
};
/*
AI解析：
把“手动管理资源”的重担，外包给了 C++ 标准库的 std::unique_ptr。
这意味着你不再需要手写析构函数、不再需要手写移动构造/赋值，编译器在背后全部帮你优雅地完成了。

std::unique_ptr 默认的删除器是 std::default_delete<T>，
它执行的是 delete ptr。对于 FILE*，我们不能用 delete，必须用 fclose
&std::fclose 的类型是 int (*)(FILE*)（函数指针）。

删除拷贝：std::unique_ptr 本身就不可拷贝。所以就算如果不写这两行 = delete，
编译器生成的默认拷贝构造函数也会因为 fp_ 不可拷贝而报错。明确写出来，意图更清晰，防止 double free。

默认移动：std::unique_ptr 天然支持移动。写 = default，
编译器会自动生成移动构造和移动赋值。移动时，fp_ 内部指针转移，源对象置空，完美安全


*/

struct BadNode
{
    std::shared_ptr<BadNode> next;
};

struct Node
{
    std::string name;
    std::shared_ptr<Node> next;
    std::weak_ptr<Node> prev; // weak_ptr 不增加强引用计数

    explicit Node(std::string n) : name(std::move(n)) {}

    ~Node()
    {
        std::cout << "~Node " << name << '\n';
    }
};

/******************************************************************************/
int main()
{
    {
        auto a = std::make_shared<BadNode>();
        auto b = std::make_shared<BadNode>();
        a->next = b;
        b->next = a;
        /*
        AI解析：
        a 指向一个 BadNode 对象（假设为 NodeA），NodeA 内部有 shared_ptr 成员 next。
        此时 NodeA 的强引用计数（use_count）为 1（被变量 a 持有）。

        b 指向一个 BadNode 对象（假设为 NodeB）。
        此时 NodeB 的强引用计数为 1（被变量 b 持有）

        将变量 b 赋值给 NodeA 内部的 next 成员。
        关键点：next 是一个 shared_ptr。这导致 NodeB 的强引用计数 +1，变为 2
        将变量 a 赋值给 NodeB 内部的 next 成员。
        导致 NodeA 的强引用计数 +1，变为 2

        离开作用域（函数结束）：
        栈上的局部变量 a 和 b 被销毁。
        NodeA 的强引用计数从 2 降为 1（此时只剩 NodeB 内部的 next 指向它）。
        NodeB 的强引用计数从 2 降为 1（此时只剩 NodeA 内部的 next 指向它）。

        死结形成：NodeA 说“我需要 NodeB 释放后我才释放”，NodeB 说“我需要 NodeA 释放后我才释放”。
        由于两者的计数都停留在 1，都不会触发析构函数，从而导致堆内存泄漏。这就是循环引用
        */
    }

    {
        auto a = std::make_shared<Node>("A");
        auto b = std::make_shared<Node>("B");

        a->next = b;
        b->prev = a;
        // NodeB 的 prev 弱引用 NodeA。
        // NodeA 的强引用计数依然为 1（weak_ptr 不增加强引用计数，它会增加一个独立的“弱引用计数”，用于管理控制块）

        std::cout << "a.use_count() = " << a.use_count() << '\n'; // 输出 1
        std::cout << "b.use_count() = " << b.use_count() << '\n'; // 输出 2

        if (auto pa = b->prev.lock())
        {
            std::cout << "b->prev 指向 " << pa->name << '\n';
        }
        else
        {
            std::cout << "b->prev 已失效\n";
        }
        /*
        为什么不能直接 b->prev->name？ 因为 weak_ptr 不保证对象还活着，可能已经被销毁了。
        正确姿势：必须调用 .lock()。它尝试将 weak_ptr 提升为 shared_ptr。
        如果对象还活着，返回一个有效的 shared_ptr（此时强引用计数临时 +1，保证在使用 pa 期间对象不被销毁）。
        如果对象已死，返回 nullptr。
        此处输出：b->prev 指向 A。
        */
    }
    /*
    离开作用域（完美析构）：
        局部变量 a 销毁 -> NodeA 强引用计数从 1 降为 0。
        NodeA 触发析构，打印 ~Node A。在析构过程中，NodeA 的成员 next（强引用 NodeB）被销毁。
        NodeB 强引用计数从 2 降为 1（只剩下局部变量 b）。
        局部变量 b 销毁 -> NodeB 强引用计数从 1 降为 0。
        NodeB 触发析构，打印 ~Node B。
        内存完全释放，无泄漏。
    */

    /*
    强引用计数（use_count）：记录有多少个 shared_ptr 指向对象。为 0 时，销毁对象。
    弱引用计数（weak_count）：记录有多少个 weak_ptr 指向对象。
    当强引用计数为 0，且弱引用计数也为 0 时，销毁控制块本身。

    weak_ptr 永远不要直接解引用，必须调用 .lock()。
    shared_ptr weak_ptr 需要维护控制块的线程安全计数，是有原子操作开销的。不要无脑用
    */

    try
    {
        FileGuard2 f("file_guard_ok.txt", "w");
        std::fputs("hello\n", f.get());
        std::puts("正常打开 file_guard_ok.txt");
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "打开失败：%s\n", e.what());
    }
    // 当代码执行到 try 块末尾的 } 时，局部对象 f 离开作用域，
    // 自动调用 ~FileGuard() 析构函数，内部执行 std::fclose(fp_)。

    try
    {
        FileGuard2 f("no_such_dir_12345/file.txt", "r");
        std::puts("不应执行：打开失败测试居然成功");
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "预期失败: %s\n", e.what());
    }
    // 构造函数抛出异常时，析构函数不会被调用

    try
    {
        FileGuard3 f("test_ok.txt", "w");
        std::fputs("hello RAII\n", f.get());
        std::puts("Success: file opened and written.");
        // 离开作用域时，f 自动调用 fclose
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "Unexpected error: %s\n", e.what());
    }

    try
    {
        FileGuard3 f("no_such_dir_12345/file.txt", "r");
        std::puts("ERROR");
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "Failure: %s\n", e.what());
    }
}