/*
自测练习使用。无其他用途
*/


#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

class FileGuardBroken {
private:
    FILE *fp_;

public:
    FileGuardBroken(const char *filename, const char *mode)
        : fp_(std::fopen(filename, mode)) {
        if (!fp_) {
            throw std::runtime_error(std::string("fopen failed: ") + filename);
        }
    }

    ~FileGuardBroken() {
        if (fp_) {
            std::fclose(fp_);
        }
    }

    // 故意不写：
    // FileGuardBroken(const FileGuardBroken&) = delete;
    // FileGuardBroken& operator=(const FileGuardBroken&) = delete;

    FILE *get() const { return fp_; }
};



int main() {
    FileGuardBroken f1("double_free_test.txt", "w");
    std::fputs("first\n", f1.get());

    {
        FileGuardBroken f2 = f1; // 默认拷贝构造：f2.fp_ == f1.fp_
        std::fputs("second\n", f2.get());
        std::cout << "f2 即将析构，会 fclose 同一个 FILE*\n";
    } // f2 析构，关闭 FILE*

    std::cout << "内层作用域结束,f1.fp_ 仍非空,f1 析构时会再次 fclose\n";
    return 0;
}


/*
运行结果：
f2 即将析构，会 fclose 同一个 FILE*
内层作用域结束,f1.fp_ 仍非空,f1 析构时会再次 fclose
free(): double free detected in tcache 2
已中止                  （核心已转储） ./RAII_V2

原因：
- `f2 = f1` 是默认拷贝构造，两个对象里的 `fp_` 指向同一个 `FILE*`。
- `f2` 先离开作用域，调用 `fclose(f2.fp_)`。
- `f1` 后离开作用域，再次调用 `fclose(f1.fp_)`。
- 同一个 `FILE*` 被关闭两次，触发 double free / 未定义行为，通常直接崩溃。
*/


/*
gdb 查看 bt
#0  __pthread_kill_implementation (threadid=<optimized out>, signo=6, no_tid=0)
    at ./nptl/pthread_kill.c:44
#1  __pthread_kill_internal (threadid=<optimized out>, signo=6) at ./nptl/pthread_kill.c:89
#2  __GI___pthread_kill (threadid=<optimized out>, signo=signo@entry=6) at ./nptl/pthread_kill.c:100
#3  0x00007ffff7845b7e in __GI_raise (sig=sig@entry=6) at ../sysdeps/posix/raise.c:26
#4  0x00007ffff78288ec in __GI_abort () at ./stdlib/abort.c:77
#5  0x00007ffff7829979 in __libc_message_impl (vma_name=vma_name@entry=0x7ffff79db569 "glibc: fatal", 
    fmt=fmt@entry=0x7ffff79debbe "%s\n") at ../sysdeps/posix/libc_fatal.c:138
#6  0x00007ffff78b08ac in __libc_message_wrapper (vmaname=0x7ffff79db569 "glibc: fatal", 
    fmt=0x7ffff79debbe "%s\n") at ../include/stdio.h:203
#7  malloc_printerr (str=str@entry=0x7ffff79e1d40 "free(): double free detected in tcache 2")
    at ./malloc/malloc.c:5341
#8  0x00007ffff78b55c8 in tcache_double_free_verify (e=<optimized out>) at ./malloc/malloc.c:3159
#9  0x00007ffff788bbce in _IO_deallocate_file (fp=0x55555556d020) at ./libio/libioP.h:970
#10 _IO_new_fclose (fp=0x55555556d020) at ./libio/iofclose.c:74
#11 0x00005555555566e7 in FileGuardBroken::~FileGuardBroken (this=0x7fffffffe178) at RAII_V2.cpp:25
#12 0x000055555555642f in main () at RAII_V2.cpp:50

//关键：#8  #11  
//#0 是最内层、最后执行的地方
//#12 是最外层、最早调用的地方

AI 关键帧解释
#12 main () at RAII_V2.cpp:50
这是你的主函数。第 50 行大概率是某个作用域 } 结束，触发局部对象析构。
#11 FileGuardBroken::~FileGuardBroken (this=0x7fffffffe178) at RAII_V2.cpp:25
这是最关键的一帧。
this=0x7fffffffe178 是当前正在析构的 FileGuardBroken 对象的地址。
RAII_V2.cpp:25 是析构函数里调用 fclose 的那一行。
说明：第二次 fclose 发生在这个对象的析构函数里。
#10 _IO_new_fclose (fp=0x55555556d020) at ./libio/iofclose.c:74
这是 glibc 的 fclose 实现。
注意这里 fp=0x55555556d020，这就是你的 FILE* 地址。
#9 _IO_deallocate_file (fp=0x55555556d020)
fclose 内部开始释放 FILE 结构，传入的还是同一个地址 0x55555556d020。
#8 tcache_double_free_verify (e=<optimized out>)
glibc 的 tcache 是线程本地缓存，用来加速小内存分配。
它在释放内存前会检查：这个指针是不是已经在 tcache 里了？
结果发现：是。于是判定 double free。
#7 malloc_printerr (str=... "free(): double free detected in tcache 2")
打印错误信息。
#4 __GI_abort ()
glibc 决定直接终止程序。
#0 ~ #3
这些是 abort 内部通过信号杀掉自己的过程，不是你的代码问题，不用管。
*/

/*
valgrind 输出结果一大大堆
AI 提取关键字：
Address 0x... is ... inside a block of size 472 free'd
→ 正在访问一块已经被 free 掉的内存。
→ 这里 472 字节就是 glibc 的 FILE 结构体大小。

Block was alloc'd at ... fopen ... FileGuardBroken::FileGuardBroken
→ 这块内存原本是 fopen 分配的，来自构造函数。
→ 说明 FILE* 的所有权被复制了。

Invalid free
→ double free 的铁证。
→ 对应 gdb 里的 free(): double free detected in tcache 2。
*/