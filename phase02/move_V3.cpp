#include <iostream>
#include <cstring>
#include <stdexcept>
#include <utility>

class MyString {
private:
    char* data_;
    std::size_t len_;

public:
    MyString(const char* s = "") {
        len_ = std::strlen(s);
        data_ = new char[len_ + 1];
        std::strcpy(data_, s);
        std::cout << "构造: " << data_ << '\n';
    }

    MyString(const MyString& other) {
        len_ = other.len_;
        data_ = new char[len_ + 1];
        std::strcpy(data_, other.data_);
        std::cout << "拷贝构造: " << data_ << '\n';
    }

    // 故意不写 noexcept，并且抛异常
    MyString(MyString&& other)
        : data_(nullptr), len_(0)
    {
        throw std::runtime_error("移动构造测试异常");
        // 下面这行永远执行不到
        // data_ = other.data_;
        // len_ = other.len_;
        // other.data_ = nullptr;
        // other.len_ = 0;
    }

    ~MyString() {
        std::cout << "析构: " << (data_ ? data_ : "(null)") << '\n';
        delete[] data_;
    }

    const char* c_str() const { return data_ ? data_ : "(null)"; }
};


int main() {
    MyString a("hello");
    std::cout << "a = " << a.c_str() << '\n';

    try {
        std::cout << "准备移动构造 v\n";
        MyString v = std::move(a);
        std::cout << "v = " << v.c_str() << '\n';
    } catch (const std::exception& e) {
        std::cout << "捕获异常: " << e.what() << '\n';
    }

    std::cout << "a = " << a.c_str() << '\n';
    std::cout << "main 结束\n";
}


/*
构造: hello
a = hello
准备移动构造 v
捕获异常: 移动构造测试异常
a = hello
main 结束
析构: hello
*/


/*
AI 解析：

v 根本没有被构造出来
MyString v = std::move(a); 调用的是移动构造。移动构造在函数体里抛异常，说明：
v 的构造过程没有完成。
所以 v 的析构函数不会被调用。
输出里没有 析构: (null) 或 析构: v，证明这一点。
C++ 规则：构造函数抛异常时，该对象的析构函数不会被调用，但已经构造完成的成员和基类会按逆序析构。

a 保持原样
因为异常是在真正“偷走” a 的资源之前抛出的，所以：
a.data_ 仍然指向 "hello"。
a 离开作用域时正常析构，打印 析构: hello。

如果你把 throw 放到移动赋值之后：
那么 a 会被置空，但 v 又没构造成功，资源就泄漏了。

如果移动构造标记了 noexcept，vector 扩容时放心用移动。
如果移动构造没有 noexcept，vector 扩容时为了强异常安全保证，会改用拷贝构造。
为什么？
因为 vector 扩容时要把旧元素搬到新内存：
如果移动过程中抛异常，旧元素已经被改，新元素又没搬完，vector 无法恢复到原状。
如果拷贝过程中抛异常，旧元素原封不动，vector 可以安全回滚。

如果移动构造标记了 noexcept 却抛异常
那么异常不会正常传播，而是直接调用 std::terminate()，程序崩溃。
因为 noexcept 是承诺，你违反了承诺，运行时就只能终止
*/