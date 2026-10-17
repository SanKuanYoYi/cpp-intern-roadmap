/*
自测练习使用。无其他用途
*/

#include <iostream>
#include <cstring>

class MyString
{
private:
    char *data_;
    unsigned int len_;

public:
    MyString(const char *newData = "");

    MyString(const MyString &other);
    MyString &operator=(const MyString &other);

    MyString(MyString &&other) noexcept;
    MyString &operator=(MyString &&other) noexcept;

    ~MyString();

    const char *getString() const { return data_; }
    const unsigned int getLen() const { return len_; }
};

MyString::MyString(const char *newData)
{
    len_ = std::strlen(newData);
    data_ = new char[len_ + 1];
    std::strcpy(data_, newData);
}

MyString::~MyString()
{
    delete[] data_;
    len_ = 0;
}

MyString::MyString(const MyString &other)
{
    len_ = other.getLen();
    data_ = new char[len_ + 1];
    std::strcpy(data_, other.data_);
}

MyString &MyString::operator=(const MyString &other)
{
    if (this != &other)
    {
        delete[] data_;
        len_ = other.getLen();
        data_ = new char[len_ + 1];
        std::strcpy(data_, other.data_);
    }
    return *this;
}
// delete[] data_ 之后如果 new 抛异常，this->data_ 变成悬空指针，析构时会再次 delete[]，导致未定义行为。
// 应该写swap
/*
最开始写的 *this == other 判断自赋值
*this 和 other 都是对象，不是地址。
问题在于：
值相等 ≠ 同一个对象。
比如两个不同的对象恰好所有成员值都相同，*this == other 会返回 true，但它们并不是同一个对象。
如果因此直接 return *this，就会错误地跳过赋值，导致逻辑错误。

查资料：
对比	类型	是否相同
this vs &other	Foo* vs Foo*	✅ 相同（都是指针）
*this vs other	Foo& vs Foo&	✅ 相同（都是引用）
this vs *this	Foo* vs Foo&	❌ 不同（指针 vs 引用）
this vs other	Foo* vs Foo&	❌ 不同（指针 vs 引用）
*/

MyString::MyString(MyString &&other) noexcept
{
    data_ = other.data_;
    len_ = other.getLen();
    other.data_ = nullptr;
    other.len_ = 0;
}
/*
other.getData()赋值，但返回 const 版本，类型不对；也多一次函数调用
应该直接用other.data_
*/

MyString &MyString::operator=(MyString &&other) noexcept
{
    // if (this != &other)
    // {
    //     delete[] data_;
    //     len_ = other.getLen();
    //     data_ = new char[len_ + 1];
    //     std::strcpy(data_, other.data_);
    //     other.data_ = nullptr;
    //     other.len_ = 0;
    // }
    // return *this;
    if (this != &other)
    {
        delete[] data_;
        data_ = other.data_;
        len_ = other.len_;
        other.data_ = nullptr;
        other.len_ = 0;
        //std::swap(data_, other.data_);
        //std::swap(len_,  other.len_);
        //也可以
    }
    return *this;
}

/*
刚开始写的是是重新 new 再 strcpy，是 O(n) 深拷贝。
它没有释放 other 原来的 data_，却直接 other.data_ = nullptr;，导致源对象原来的堆内存泄漏。
delete[] data_ 之后如果 new 抛异常，this->data_ 变成悬空指针，析构时会再次 delete[]，导致未定义行为。
*/

/*
刚开始不理解为什么一个置空一个不
拷贝赋值是“复制”，原件不能动；const
移动赋值是“偷”，偷完必须把原主的手清空，不能const
否则原主析构时会把你偷来的东西再砸一遍，导致双重释放。
*/