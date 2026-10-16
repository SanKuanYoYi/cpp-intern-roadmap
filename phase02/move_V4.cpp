#include <iostream>
#include <vector>
#include <cstring>

class MyString {
private:
    char* data_;
    std::size_t len_;

public:
    MyString(const char* s = "") {
        len_ = std::strlen(s);
        data_ = new char[len_ + 1];
        std::strcpy(data_, s);
    }

    MyString(const MyString& other) {
        len_ = other.len_;
        data_ = new char[len_ + 1];
        std::strcpy(data_, other.data_);
        std::cout << "  拷贝构造\n";
    }

    MyString(MyString&& other) noexcept
        : data_(other.data_), len_(other.len_)
    {
        other.data_ = nullptr;
        other.len_ = 0;
        std::cout << "  移动构造\n";
    }

    ~MyString() {
        delete[] data_;
    }
};

int main() {
    std::vector<MyString> v;

    for (int i = 0; i < 10; ++i) {
        std::cout << "push_back #" << i
                  << "  size=" << v.size()
                  << "  capacity=" << v.capacity() << '\n';
        v.push_back(MyString("x"));
        std::cout << "  -> 插入后 size=" << v.size()
                  << "  capacity=" << v.capacity() << '\n';
    }

    std::cout << "最终 size=" << v.size()
              << "  capacity=" << v.capacity() << '\n';
}

/*
push_back #0  size=0  capacity=0
  移动构造
  -> 插入后 size=1  capacity=1
push_back #1  size=1  capacity=1
  移动构造          <- 临时对象移动到新元素
  移动构造          <- 扩容，旧元素搬到新内存
  -> 插入后 size=2  capacity=2
push_back #2  size=2  capacity=2
  移动构造
  移动构造
  移动构造
  -> 插入后 size=3  capacity=4
push_back #3  size=3  capacity=4
  移动构造
  -> 插入后 size=4  capacity=4
push_back #4  size=4  capacity=4
  移动构造
  移动构造
  移动构造
  移动构造
  移动构造
  -> 插入后 size=5  capacity=8
push_back #5  size=5  capacity=8
  移动构造
  -> 插入后 size=6  capacity=8
push_back #6  size=6  capacity=8
  移动构造
  -> 插入后 size=7  capacity=8
push_back #7  size=7  capacity=8
  移动构造
  -> 插入后 size=8  capacity=8
push_back #8  size=8  capacity=8
  移动构造
  移动构造
  移动构造
  移动构造
  移动构造
  移动构造
  移动构造
  移动构造
  移动构造
  -> 插入后 size=9  capacity=16
push_back #9  size=9  capacity=16
  移动构造
  -> 插入后 size=10  capacity=16
最终 size=10  capacity=16
*/


/*
移动构造发生在两个地方：
临时对象 MyString("x") 移动到 vector 的新元素：每次 push_back 调用 1 次。
扩容时旧元素搬到新内存：每次扩容搬 size() 个元素。
*/


/*
noexcept 移动构造 → 用移动
没有 noexcept → 用拷贝
*/