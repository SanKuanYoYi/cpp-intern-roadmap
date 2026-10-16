#include <stdexcept>
#include <iostream>
#include <utility>   // std::swap, std::move, std::move_if_noexcept
#include <string>    // std::string
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

/*
普通 new:
  operator new  →  分配对象壳  →  构造函数  →  对象可用

普通 delete:
  析构函数  →  清理对象拥有的资源  →  operator delete  →  释放对象壳

placement new:
  已有内存  →  构造函数  →  对象可用
  （不调用 operator new，不分配壳）

placement new 的清理:
  手动 ~T()  →  清理资源
  内存由外层负责（栈自动回收 / 手动 ::operator delete）
*/


/*
AI解析：
从“最终效果”上看，delete 确实等于“析构 + 释放内存”。
但为什么手写 Vector 时绝对不能直接用 delete（或 delete[]）来代替 Deallocate？

冲突一：内存的“出身”不一样（配对铁律）
new T[n] 的底层：编译器会偷偷在分配的内存块头部多申请几个字节，用来记录数组长度 n。
它返回给你的指针，其实是偏移了这几字节之后的地址。
delete[] p 的底层：它拿到指针后，会向前回溯几个字节，
读出那个隐藏的长度 n，然后循环调用 n 次析构函数，最后连头部一起释放。

而 Vector 用的是：
::operator new(capacity_ * sizeof(T))：这是纯裸内存分配，没有头部元数据。
它返回的就是内存块的绝对起始地址。
如果你对这个指针强行使用 delete[]，delete[] 会向前回溯去读那个根本不存在的“数组长度”，
导致读取越界，直接崩溃。

配对铁律：
new T[] 必须配 delete[]。
::operator new 必须配 ::operator delete。

冲突二：new T[] 会“擅自”构造所有元素（性能与约束）
这是 std::vector 绝对不用 new T[] 的根本原因。
如果你用 new T[capacity_] 来分配内存：
它会立刻调用 T 的默认构造函数，构造出 capacity_ 个对象。
这意味着 T 必须有默认构造函数。没有默认构造函数，代码直接编译失败。
你仅仅是想预留内存（reserve），结果却把整个数组都默认构造了一遍。
如果 T 是 std::string，它会分配一堆空字符串；如果 T 是 MyBigData，它会初始化一堆大对象，性能直接崩盘。

Vector 的做法：
::operator new 分配了裸内存，不构造任何对象。只有当你 push_back 时，才用 placement new 按需构造。
capacity_ 只代表“能装多少”，不代表“已经构造了多少”。
因此，在释放时，你根本不能指望 delete[] 替你自动析构，
因为它不知道你到底手动构造了几个（size_ 个，而不是 capacity_ 个）。
你必须手动循环 size_ 次，逐个调用 ~T()。

冲突三：范围控制——delete[] 分不清 size_ 和 capacity_
delete[] 的逻辑是：“释放这整个数组，对数组里每一个元素调用析构函数”。
但在 Vector 里：
size_ = 3：只有下标 0、1、2 是真正构造过的对象。
capacity_ = 10：下标 3 到 9 是原始内存，里面没有活着的对象。
如果你调用 delete[] data_，它会试图对下标 3 到 9 也调用析构函数。
对未构造的裸内存调用 ~T()，是典型的未定义行为。
如果 T 是 std::string，它会去释放一个野指针，程序当场崩溃。
而 Deallocate 循环条件是 i < size_，精准避开那些尚未构造的裸内存区域。
*/