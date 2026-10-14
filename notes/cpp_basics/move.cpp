#include <cstddef>
#include <cstring>
#include <string>


class MyString{
private:
    char* data_;        // 指向堆上分配的字符串数组
    std::size_t len_;   // 字符串长度（不含 '\0')
    //如果每次 size() 都调用 strlen(data_)，复杂度是 O(n)。
    //维护一个 len_ 成员，可以让 size() 变成 O(1)，这也正是 std::string 的做法。

public:
    MyString(const char* s = "");

    MyString(const MyString& other);            // 拷贝构造
    MyString& operator=(const MyString& other); // 拷贝赋值

    MyString(MyString&& other) noexcept;        // 移动构造
    MyString& operator=(MyString&& other) noexcept; // 移动赋值

    ~MyString();

    const char* c_str() const { return data_; }
    size_t size() const { return len_; }
};


MyString::MyString(const char* s) {
    len_ = std::strlen(s);
    data_ = new char[len_ + 1];  // 注意 +1 给 '\0'
    std::strcpy(data_, s);
}

MyString::~MyString() {
    delete[] data_;  // 必须是 delete[] 而不是 delete
}

MyString::MyString(const MyString& other) {
    len_ = other.len_;
    data_ = new char[len_ + 1];
    std::strcpy(data_, other.data_);
}

MyString& MyString::operator=(const MyString& other) {
    if (this != &other) {  // 1. 自赋值检查
        MyString temp(other);      // 2. 拷贝构造一个临时对象
        std::swap(data_, temp.data_); // 3. 交换资源
        std::swap(len_, temp.len_);
    } // 4. temp 析构，自动释放原来的旧资源
    return *this; 
}

MyString::MyString(MyString&& other) noexcept 
    : data_(other.data_), len_(other.len_) {
    other.data_ = nullptr;  // 关键！
    other.len_ = 0;
}
//必须把 other.data_ 置为 nullptr。否则 other 析构时会释放掉已经被窃取的内存，导致悬空指针。
//必须加 noexcept。如果移动构造不声明 noexcept，当 std::vector<MyString> 扩容时，
//为了保证强异常安全，标准库会放弃使用移动构造，退而求其次使用拷贝构造，导致性能骤降。

MyString& MyString::operator=(MyString&& other) noexcept {
    if (this != &other) {
        delete[] data_;       // 1. 释放自己原有的资源
        data_ = other.data_;  // 2. 窃取对方资源
        len_ = other.len_;
        other.data_ = nullptr; // 3. 对方置空
        other.len_ = 0;
    }
    return *this;
}


/*
访问接口：c_str() 和 size()
const char* c_str() const { return data_; }
size_t size() const { return len_; }

const 修饰函数：表示该函数不会修改对象的成员变量。
c_str() 返回 const char*：防止外部通过指针修改内部数据，破坏封装性。
*/


/*
在 C++11 之前，只有“三法则（Rule of Three）”：
如果你需要自定义析构函数、拷贝构造、拷贝赋值，那么这三个通常都需要。

C++11 引入移动语义后，变成了五法则（Rule of Five）：如果你需要自定义其中一个，通常五个都需要。
*/


/*
了解:
Modern C++  零法则
class MyString {
    std::string data_; // 直接用标准库的 string
public:
    MyString(const char* s = "") : data_(s) {}
    const char* c_str() const { return data_.c_str(); }
    size_t size() const { return data_.size(); }
    
    // 所有拷贝、移动、析构，编译器默认生成，无需手写！
};

因为 std::string 自己已经完美管理了内存。你把资源管理的责任“委托”给了标准库
这就是零法则——当你的类成员都能自我管理资源时，你不需要写任何特殊成员函数。
*/