

----------------------------------------------------------------------------
智能指针 (外部资料)
----------------------------------------------------------------------------
特性	unique_ptr	            shared_ptr
所有权	独占	                共享
拷贝	禁止	                允许，拷贝增加强引用计数
移动	允许	                允许
开销	通常一个指针，          对象指针 + 控制块指针，
        删除器是类型一部分	    控制块有原子计数
删除器	编译期类型的一部分	        放在控制块里，运行时类型擦除
生命周期	一个所有者销毁即释放	最后一个强引用销毁才释放
适用场景	单一所有者、RAII 包装	多所有者、共享生命周期

简言之：unique_ptr 表达“只有我拥有”；shared_ptr 表达“大家一起拥有”。


shared_ptr 的引用计数怎么实现的？
通常有一个堆上的控制块
struct ControlBlock {
    std::atomic<long> shared_count; // 强引用计数
    std::atomic<long> weak_count;   // 弱引用计数
    Deleter deleter;
    Allocator allocator;
};


weak_ptr 主要解决循环引用
两个对象互相用 shared_ptr 指向对方，强引用计数永远不归零，导致内存泄漏。把其中一个方向改成 weak_ptr 即可打破循环。

shared_ptr 有线程安全问题
如果多个线程同时读写同一个 shared_ptr 变量，例如一个线程赋值、另一个线程读取，会产生数据竞争。
如果当前线程通过裸指针访问对象，而另一个线程销毁了最后一个 shared_ptr，就会悬空。


make_shared 和 shared_ptr<T>(new T) 区别

内存分配次数（最核心区别）
shared_ptr<T>(new T)：需要 2 次 内存分配。
第一次：new T 在堆上分配对象本身的内存。
第二次：shared_ptr 构造函数在堆上分配一个控制块（Control Block，用来存引用计数、弱引用计数、删除器等）。
这两块内存是分离的，内存碎片多。
make_shared<T>()：只需要 1 次 内存分配。
它会在堆上分配一块足够大的连续内存，把“对象本身”和“控制块”放在一起（一次 malloc 搞定）。
内存布局更紧凑，极大减少了内存碎片。

异常安全性（极其重要）
shared_ptr<T>(new T) 有隐患：
想象一个函数调用 func(shared_ptr<T>(new T), otherFunc());。C++ 标准不规定函数参数的求值顺序。如果编译器先执行 new T，然后去执行 otherFunc()，结果 otherFunc() 抛出了异常。此时 new T 返回的裸指针还没来得急交给 shared_ptr，就会导致内存泄漏。
make_shared<T>() 绝对安全：
因为 make_shared 是一个函数，它内部先分配内存，再构造对象，最后返回 shared_ptr。中间任何一个环节抛出异常，都会自动清理，不会发生泄漏。

make_shared 缺点
无法自定义删除器（比如你想用 fclose 关文件，就必须用 shared_ptr<FILE>(new FILE, &fclose)，不能用 make_shared）。
延迟释放内存：由于对象和控制块绑在一起，只要还有一个 weak_ptr 存活，整个内存块（包括已经不需要的大对象）就无法归还给操作系统。
一般优先用 make_shared
----------------------------------------------------------------------------
----------------------------------------------------------------------------

----------------------------------------------------------------------------
placement new (外部资料)
----------------------------------------------------------------------------
普通 new vs placement new
普通 new
T* p = new T(value);
做两件事：从堆上分配内存。在这块内存上构造对象。
new (data_ + size_) T(value);
在已经分配好的指定内存地址上构造对象。不分配新内存。
new (地址) 类型(构造参数);

new (data_ + size_) T(value);
假设 data_ 是一块已经分配好的原始内存（比如 char* 或 T*），size_ 是当前已存放的元素个数。
data_ + size_：指向下一个空闲位置的地址。
new (data_ + size_) T(value)：在这个地址上，用 value 构造一个 T 对象。
这通常是手写容器（比如自己实现 vector）时的写法：

为什么需要 placement new？
因为有时候内存已经分配好了，但里面还没有对象。你需要在指定位置构造对象，而不是重新分配。
常见场景：
手写容器：预先分配一大块内存，逐个构造元素。
内存池：从池子里拿一块内存，在上面构造对象。
避免频繁分配释放：复用已分配的内存。

placement new 的配套操作
1.需要手动调用析构函数
placement new 构造的对象，不能直接用 delete 释放，因为 delete 会试图释放内存（而这块内存不是你用普通 new 分配的）。
正确做法：
p->~T();   // 显式调用析构函数，销毁对象
// 内存本身由外层管理（比如 data_ 是 new char[] 分配的，最后 delete[] data_）
2.需要包含头文件
#include <new>
----------------------------------------------------------------------------
----------------------------------------------------------------------------



----------------------------------------------------------------------------
杂记 (一些笔记粘贴)
----------------------------------------------------------------------------

打开文件
    fp = fopen("a.txt", "r");  // 以只读方式打开文件
    fopen 返回一个 FILE*，若打开失败则返回 NULL。
    常见模式包括：
    "r"：只读
    "w"：只写（清空原内容）
    "a"：追加写入
    "r+"、"w+"、"a+"：读写模式
    "rb"、"wb" 等：二进制文件模式
    读写文件
    读取：fread、fgetc、fgets、fscanf 等
    写入：fwrite、fputc、fputs、fprintf 等
    这些函数都需要 FILE *fp 作为文件流对象


puts：int puts(const char *str);
把字符串写到 标准输出 stdout，并在末尾自动追加一个换行符 \n。
只能输出字符串，不能输出数字、变量格式。
成功返回非负值，失败返回 EOF。

fputs：int fputs(const char *str, FILE *stream);
把字符串写到指定的文件流，不会自动加换行。
可以写文件、写 stdout、写 stderr。
成功返回非负值，失败返回 EOF。

fprintf：int fprintf(FILE *stream, const char *format, ...);
按格式字符串把内容写到指定流。
支持 %d、%f、%s、%c 等，和 printf 用法一样，只是多了第一个参数 FILE *。
不会自动换行，需要自己在格式里写 \n。
成功返回写入的字符数，失败返回负值。


左值：有名字、有固定内存地址、生命周期较长的变量。比如 int a = 10; 里的 a。
右值：临时的、没有名字的、马上就要死掉的值。比如 10、a + b 的结果、或者函数返回的临时对象。


explicit 
用于修饰类的构造函数，它的唯一作用是：禁止隐式类型转换。


std::move 
本身不移动任何东西。它只是一个类型转换，把一个左值强制转换成了右值引用。真正发生资源转移的，是随后调用的移动构造函数或移动赋值函数。
被移动后的对象处于“有效但未指定状态”（valid but unspecified state）。
有效：对象仍然是一个合法对象，可以析构、可以重新赋值，通常也可以调用不依赖具体值的成员函数。
未指定：不能假设它的值是什么，不要读取或依赖它的旧值。


noexcept 
它是给编译器看的一个“保证书”，承诺“我这个函数绝对不会抛出异常”。
如果移动构造没有 noexcept：vector 会想：“我要把旧元素搬到新内存。万一搬到一半，你移动构造函数抛异常了，旧内存和新内存的状态就全乱了，没法恢复。为了安全，我不敢用移动构造，我退而求其次，用拷贝构造。” —— 结果就是：性能暴跌（深拷贝极其昂贵）。
如果移动构造加了 noexcept：vector 会想：“太好了！你保证不抛异常，那我就可以放心大胆地直接把指针偷过来（移动），效率拉满。”


&&（右值引用/移动语义）
在没有 && 之前（C++98），想把一个临时的 MyString 赋值给另一个变量，只能深拷贝（重新分配内存，逐个字符复制）。极其浪费，因为那个临时对象马上就要销毁了，拷贝完再销毁，纯属做无用功。
有了 &&（移动语义），编译器说：“既然这个临时对象马上就要死了，那我不拷贝了，我直接把它的指针偷过来（窃取资源）。”
在标准库（如 std::vector、std::string）里，到处都在用 &&。只是他们没让你写出来。


不加 ::：从最近的作用域开始找，找到局部 x 就停。
加 ::：无视局部，直接用全局 x。
不加 ::：优先匹配类内的成员函数。
加 ::：强制调用全局命名空间里的函数。
operator new（不加 ::）：优先用类内重载的版本。
::operator new：强制用全局版本，绕过类内重载。


为什么移动构造必须 noexcept？（工程铁律）
移动的语义决定了它不能失败。移动就是“偷指针”，本身就是几条汇编指令（拷贝指针、置空指针），没有任何会失败的操作。如果你的移动构造会抛异常，说明你在移动构造里做了“不该做的事”（比如分配新内存、深拷贝），那它就不配叫移动构造。
不标注 noexcept 的移动构造是“有毒资产”。std::vector、std::sort、std::swap 等标准算法会检测 noexcept 来决定是否采用移动路径。你不标 noexcept，你的类型就会在所有需要“安全移动”的场景中被退化为拷贝，性能大幅下降。
析构函数默认 noexcept，移动构造也应该遵守这个约定。
资源转移是个“基本操作”，不应该在转移过程中引入失败点。


MyString 的场景：单个对象，直接 move
MyVector 的场景：批量搬迁，必须用 move_if_noexcept


C++11 的 Lambda 表达式
[捕获列表](参数列表) -> 返回类型 { 函数体 }
// auto add = [](int a, int b) { return a + b; };
// std::cout << add(1, 2);  // 3

写法	含义
[]	不捕获任何外部变量
[&]	引用捕获所有外部变量（可读可写，但要注意生命周期）
[=]	值捕获所有外部变量（只读，除非加 mutable）
[x]	只值捕获 x
[&x]	只引用捕获 x
[x, &y]	值捕获 x，引用捕获 y
[&, x]	除 x 值捕获外，其余全部引用捕获
[=, &y]	除 y 引用捕获外，其余全部值捕获
[this]	捕获当前类的 this 指针


什么样的数据需要加锁？
满足这两个条件的才需要：
多个线程都会访问它（共享）；
至少有一个线程会修改它（写）。
void f() {
    int local = 0;           // ① 局部变量：每个线程调用 f() 时各有各的一份 → 【不需要锁】
    for (int i = 0; i < 100; ++i) local += i;
}
int counter = 0;             // 全局变量：所有线程共用同一个
void g() {
    ++counter;               // ② 修改共享数据 → 【必须锁】
}


constexpr = "编译期常量"。
翻译："这个值在编译的时候就已经确定了，编译器直接把它当常量用。"
等价于写死 xx，只是多了个名字。
int a = 3;                 // 普通变量：运行时才赋值，可以改
const int b = 3;           // 常量：运行时赋值，不能改
constexpr int c = 3;       // 编译期常量：编译时就算出来了，不能改


std::ifstream f(path, std::ios::binary | std::ios::ate)
std::ios::ate
ate = at end，打开文件后，立即把文件读指针定位到文件末尾。
注意：ate 只是初始位置在末尾，之后你仍然可以 seekg 到任意位置读写。
它和 std::ios::app 不同：app 是每次写操作前都强制定位到末尾，只影响写；ate 只影响打开时的初始位置。


tellg 
是 C++ 输入文件流（std::ifstream / std::istream）的成员函数，作用是：返回当前读指针（get pointer）的位置（偏移量）。
te 让读指针打开后就在文件末尾，tellg() 返回的就是文件总字节数。
这是 tellg 最常见的用法之一


std::ofstream用于写文件操作，
而std::ifstream用于读文件操作
std::ios::trunc 用于在打开文件时清空其内容。如果文件存在，会被截断为长度 0


std::streamsize 是 C++ 输入/输出库中定义的一种有符号整数类型，
用于表示 I/O 操作中传输的字符数或 I/O 缓冲区的大小。它被定义在头文件 <ios> 中，
通常作为 std::size_t 的有符号对应类型


int main(int argc, char* argv[])
这是 C/C++ 程序接收命令行参数的标准入口签名。
int argc	argument count：命令行参数的个数。
char* argv[]	argument vector：参数字符串数组（等价于 char** argv）

假设在终端敲：
./copy_single test.bin out.bin
程序启动时操作系统把这个命令行切分成"以空白分隔的 token"，装进 argv：
argc = 3
argv[0] = "./copy_single"   ← 程序自身路径/名字（不是第一个参数！）
argv[1] = "test.bin"
argv[2] = "out.bin"
argv[3] = nullptr           ← 数组末尾永远是空指针，即 C 字符串数组的"哨兵"



对比项	std::runtime_error	                std::cerr
是什么	异常类（std::exception 的派生类）	标准错误输出流（std::ostream 对象）
头文件	<stdexcept>	                       <iostream>
用途	抛出异常，中断程序流程	                输出错误信息到终端（不中断程序）
语法	throw std::runtime_error("msg");	std::cerr << "msg" << std::endl;
是否需要 catch	不 catch 会终止程序（调用 std::terminate）	不需要，直接输出
能否被捕获	✅ 可以 try/catch	            ❌ 不是异常，不能被 catch



std::fixed 是 C++ I/O 流中的一种格式控制操纵器，用于将浮点数以定点小数形式输出，而不是默认的科学计数法。当与 std::setprecision 搭配使用时，setprecision 会指定小数点后的位数。



seekg 与 tellg 的配合
tellg()：问当前指针在哪。
seekg()：让指针去哪。



C++ 标准库 <fstream> 提供了三个类：
std::ifstream：读文件（Input File Stream）。
std::ofstream：写文件（Output File Stream）。
std::fstream：既能读又能写（双向文件流），少见

ofstream 默认会清空原文件。如果你想追加内容，或者写二进制文件，必须在第二个参数指定模式：
// 追加模式 (Append)
std::ofstream out("log.txt", std::ios::app); 
// 二进制模式 (Binary) - 用于存图片、结构体等
std::ofstream out("data.bin", std::ios::binary);
// 也可以组合使用
std::ofstream out("log.bin", std::ios::app | std::ios::binary);

常用模式速查：
std::ios::in：读（ifstream 默认）。
std::ios::out：写（ofstream 默认）。
std::ios::app：追加到文件末尾，不清空。
std::ios::trunc：如果文件存在，先清空（ofstream 默认）。
std::ios::binary：以二进制形式读写（不做换行符转换）。

AI 提示：
文本模式（默认）：在 Windows 下，遇到 \n 会自动转成 \r\n 写入，读取时又把 \r\n 转成 \n。
但在 Linux 下，不会有这个转换。
二进制模式：原封不动地读写。如果你跨平台处理文件，或者读写图片、音频、结构体，
必须加 std::ios::binary，否则数据会被悄悄修改。



string.append() 和 += 运算符的核心目的都是在字符串末尾追加内容。
在绝大多数日常场景下，它们的性能完全一样，但它们的功能丰富度和底层细节有所区别
当你需要从另一个字符串中截取一部分追加，或者使用迭代器时，append() 是不可替代的

1.追加另一个字符串的子串（最常用）
std::string base = "abcdef";
std::string str = "123";
// 需求：把 base 从索引 2 开始的 3 个字符（"cde"）追加到 str 后面
// ❌ 用 += 的做法（会产生一个临时 string 对象，有性能损耗）
str += base.substr(2, 3); 
// ✅ 用 append 的做法（直接在原字符串后拼接，零临时对象）
str.append(base, 2, 3); // 结果 str 变成 "123cde"

2.追加指定数量的某个字符
std::string s = "A";
// 用 += 只能一个个加
s += '='; s += '='; s += '='; 
// 用 append 可以直接追加 3 个 '='
s.append(3, '='); // 结果 s 变成 "A==="

3.使用迭代器追加
std::string s = "A";
std::vector<char> v = {'1', '2', '3'};
// append 支持传迭代器范围
s.append(v.begin(), v.end()); // 结果 s 变成 "A123"

对于追加整个字符串：没有差异，性能一样。
对于追加子串：append(str, pos, len) 比 += str.substr(pos, len) 更快。
因为 substr 会先构造一个新的临时 string 对象并分配堆内存，然后再追加；
而 append 则是直接按长度把数据拷过去，省去了一次内存分配和构造的开销。



string.size() 还是 string.length()
完全等价——length() 是历史遗留别名，size() 更符合 STL 惯例


