#include <stdexcept>
#include <iostream>
#include <utility>   // std::swap, std::move, std::move_if_noexcept
#include <string>    // main 里用了 std::string
#include <cstddef>   // size_t

template <typename T>
class Vector
{
private:
    T *data_;
    size_t size_;
    size_t capacity_;

    // 更新内容
    void swap(Vector &other) noexcept
    {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
        std::swap(capacity_, other.capacity_);
    }

    void DeallocateAll()
    {
        for (size_t i = 0; i < size_; ++i)
        {
            data_[i].~T();
        }
        ::operator delete(data_);
        data_ = nullptr;
        size_ = 0;
    }
    /*
    AI解析：
    不能直接 delete[] data_？
    因为你的 Vector 用的是 placement new 构造元素，而不是 new T[]。
    new T[] 会记录数组长度，delete[] 能自动调用每一个元素的析构函数。
    placement new 只在裸内存上构造对象，内存本身是 ::operator new 分配的，没有数组元信息。
    所以你必须手动循环，对每个已经构造的元素显式调用 ~T()。
    如果 T 是 std::string、std::vector 这类管理了资源的类型，
    不调用析构函数，它们内部 new 出来的堆内存就会泄漏。

    为什么用 ::operator delete 而不是 delete data_ 或 delete[] data_？
    delete data_：会先调用一次 ~T()，再释放内存。但你已经手动调过析构了，
    再调用一次就是二次析构，未定义行为。
    delete[] data_：要求内存是用 new T[] 分配的。你的内存是用 ::operator new 分配的，配对不上。
    ::operator delete(data_)：只释放内存，不调用任何析构函数，正好匹配 ::operator new。
    加了 :: 表示调用全局的 operator delete，也就是最底层那个释放堆内存的函数。
    */

public:
    Vector() : data_(nullptr), size_(0), capacity_(0) {}
    //~Vector() { delete[] data_; }  第一次错误写法
    ~Vector()
    {
        DeallocateAll();
    }

    Vector(const Vector &other);
    Vector &operator=(const Vector &other);

    Vector(Vector &&) noexcept;
    Vector &operator=(Vector &&) noexcept;

    void push_back(const T &value);
    void push_back(T &&value);

    T &operator[](size_t index);
    const T &operator[](size_t index) const;

    size_t size() const;
    size_t capacity() const;

    bool empty() const { return size_ == 0; };

    void reserve(size_t newCapacity);
};

template <typename T>
Vector<T>::Vector(const Vector<T> &other)
    : data_(nullptr), size_(0), capacity_(0)
{
    if (other.size_ > 0)
    {
        reserve(other.size_);
        for (size_t i = 0; i < other.size_; ++i)
        {
            new (data_ + i) T(other.data_[i]); // placement new
            ++size_;
        }
    }
}
/*
资料：
拷贝构造：强异常安全（要么全成功，要么原样不变）
Placement new 是 C++ 中一种特殊的 new 形式，它不会分配内存，
而是在你提供的已有内存地址上直接调用构造函数来构造对象，例如 new(ptr) Type(args)。
它将“内存分配”和“对象构造”分离，常用于内存池、对象池、std::vector 扩容等场景。

data_ + size_：指向下一个空闲位置的地址。
new (data_ + size_) T(value)：在这个地址上，用 value 构造一个 T 对象。
*/

template <typename T>
Vector<T> &Vector<T>::operator=(const Vector<T> &other)
{
    if (this != &other)
    {
        Vector temp(other);
        swap(temp);
    }
    return *this;
}
/*
资料：
拷贝赋值：copy-and-swap 惯用法
*/

template <typename T>
Vector<T>::Vector(Vector<T> &&other) noexcept
    : data_(other.data_), size_(other.size_), capacity_(other.capacity_)
{
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}
/*
资料：
移动构造：窃取资源，源对象置空
*/

template <typename T>
Vector<T> &Vector<T>::operator=(Vector &&other) noexcept
{
    if (this != &other)
    {
        DeallocateAll();
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}
/*
资料：
移动赋值：先释放自己，再窃取
*/

template <typename T>
size_t Vector<T>::size() const
{
    return size_;
}

template <typename T>
size_t Vector<T>::capacity() const
{
    return capacity_;
}

template <typename T>
void Vector<T>::push_back(const T &value)
{
    if (size_ == capacity_)
    {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }

    new (data_ + size_) T(value);
    ++size_;
}
/*
资料：
data_ + size_：指向下一个空闲位置的地址。
new (data_ + size_) T(value)：在这个地址上，用 value 构造一个 T 对象。
*/

template <typename T>
void Vector<T>::push_back(T &&value)
{
    if (size_ == capacity_)
    {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }
    new (data_ + size_) T(std::move(value));
    ++size_;
}

template <typename T>
T &Vector<T>::operator[](size_t index)
{
    return data_[index];
}

template <typename T>
const T &Vector<T>::operator[](size_t index) const
{
    return data_[index];
}

// 资料固定扩容写法
template <typename T>
void Vector<T>::reserve(size_t newCapacity)
{
    if (newCapacity <= capacity_)
        return;

    // 1. 分配裸内存（不构造元素）
    T *new_data = static_cast<T *>(::operator new(newCapacity * sizeof(T)));

    // 2. 把旧元素搬到新内存（异常安全：用 move_if_noexcept）
    size_t i = 0;

    try
    {
        for (; i < size_; ++i)
        {
            new (new_data + i) T(std::move_if_noexcept(data_[i]));
        }
    }
    catch (...)
    {
        // 回滚已构造的元素
        for (size_t j = 0; j < i; ++j)
        {
            new_data[j].~T();
        }
        ::operator delete(new_data);
        throw; // 原对象保持完整，强异常安全
    }

    // 3. 销毁旧元素并回收旧内存
    for (size_t j = 0; j < size_; ++j)
    {
        data_[j].~T();
    }
    ::operator delete(data_);

    // 4. 接管新内存
    data_ = new_data;
    capacity_ = newCapacity;
}
/*
资料：
std::move_if_noexcept 是什么？
在 reserve 搬运旧元素时：
如果 T 的移动构造是 noexcept，用它（性能好）。
如果移动构造可能抛异常，但拷贝构造存在，就用拷贝
（因为一旦移动过程中抛异常，原数据可能已经被破坏，无法回滚）。
这是保证强异常安全的关键细节。
*/

int main()
{
    Vector<int> v;
    for (int i = 0; i < 10; ++i)
        v.push_back(i * i);
    for (size_t i = 0; i < v.size(); ++i)
        std::cout << v[i] << " ";
    std::cout << "\nsize=" << v.size() << " cap=" << v.capacity() << "\n";

    Vector<std::string> vs;
    vs.push_back("hello");
    vs.push_back(std::string("world")); // 移动版本
    for (size_t i = 0; i < vs.size(); ++i)
        std::cout << vs[i] << " ";
    std::cout << "\n";

    Vector<int> v2 = v;            // 拷贝构造
    Vector<int> v3 = std::move(v); // 移动构造，v 变空
    std::cout << "v.size=" << v.size() << " v3.size=" << v3.size() << "\n";
    return 0;
}
/*
0 1 4 9 16 25 36 49 64 81 
size=10 cap=16                  //1-2-4-8-16
hello world 
v.size=0 v3.size=10             //置空
*/