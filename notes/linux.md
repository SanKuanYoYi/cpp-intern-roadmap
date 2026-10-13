# gdb基础理解
#基础编译命令
g++ -g main.cpp student.cpp -o app 
#gdb启动
gdb ./app
命令	            简写	                作用
break main.cpp:10	b 10	            #在第 10 行设置断点
break add_student	b add_student	    #在函数 add_student 入口设断点
run	                r                   #启动程序，跑到断点停住
next	            n	                #单步执行（不进入函数内部）
step	            s	                #单步执行（会进入函数内部）
print x	            p x	                #打印变量 x 的值
continue	        c	                #继续运行到下一个断点
backtrace	        bt	                #查看当前调用栈（程序崩溃时必用）
quit	            q	                #退出 GDB

# 资料命令
启动调试：gdb ./program、gdb ./program core、gdb -p pid（attach 到运行进程）。
断点：break file:line、break func、info break、delete。
运行控制：run、continue、next、step、finish、until。
查看栈：bt（backtrace）、frame n、info locals、info args。
查看变量：print var、p *ptr、x/10x addr。
多线程：info threads、thread n、thread apply all bt（最常用！）。
core dump 分析：bt 看崩溃点，frame 切帧，info registers。
调试宏/条件断点：break func if cond。
watchpoint：watch var 监视变量变化。
修改运行时值：set var x=10。


------------------------------------------------------------------------------------
# Valgrind初识
Valgrind 是 Linux 下 C/C++ 专门用来检测内存泄漏、越界访问等内存问题
#基础命令
valgrind [valgrind-options] ./your_program [your_program-options]
#检测内存泄漏举例
valgrind --leak-check=full ./student_manager
#其他命令，看个眼熟
详细的泄漏报告valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./student_manager//--track-origins=yes 会追踪未初始化值的来源
检测多线程问题valgrind --tool=helgrind ./your_threaded_program

#举例
#include <iostream>
int main() {
    int* ptr = new int[10]; // 忘记释放内存
    return 0;
}

编译并运行 Valgrind 后
==12345== HEAP SUMMARY:
==12345==     in use at exit: 40 bytes in 1 blocks
==12345==     total heap usage: 1 allocs, 0 frees, 40 bytes allocated
==12345== 
==12345== 40 bytes in 1 blocks are definitely lost in loss record 1 of 1
==12345==    at 0x4C2FB0F: operator new[](unsigned long) (in /usr/lib/valgrind/vgpreload_memcheck-amd64-linux.so)
==12345==    by 0x4005E6: main (my_program.cpp:4)

definitely lost：确定丢失，这是真正的内存泄漏
at ... 和 by ...：这是调用栈，直接告诉你泄漏发生在 my_program.cpp 的第 4 行

#AI提示：现代 C++ 开发中，AddressSanitizer (ASan) 是 Valgrind 的有力替代品。它由编译器直接支持，速度比 Valgrind 快得多（通常只慢 2 倍左右），能检测类似的内存错误
------------------------------------------------------------------------------------



************************************************************************************
## 以下为随机场景命令使用练习 ##
************************************************************************************

# 查看当前系统所有监听端口，并找出 3306 是否在监听（MySQL 默认端口）
① ss -tuln | grep 3306
② netstat -tuln | grep 3306
③ lsof -i | grep 
输出：
tcp   LISTEN 0      151        127.0.0.1:3306       0.0.0.0:*   
tcp   LISTEN 0      70         127.0.0.1:33060      0.0.0.0:*
结论：
查监听用 ss -tuln，查端口用 grep，查进程用 lsof -i



# 对目录下的 gdb_test.cpp (有段错误) 进行调试

首先
g++ -g -O0 main.cpp -o app
./app
输出：Segmentation fault (core dumped)

在终端输入 gdb ./app，进入 GDB 交互界面。

1. 第一阶段：基础平A（断点、单步、打印）

list / l：看源码
gdb
(gdb) l
会显示当前文件的代码片段。按回车键继续往下翻。

break / b：打断点
(gdb) b main         # 在 main 函数入口打断点
(gdb) b 20           # 在当前文件第 20 行打断点（也就是 printList 里面）

run / r：启动程序
(gdb) r
程序会在断点处停下来，显示 Breakpoint 1, main () at main.cpp:...

next / n 和 step / s：单步执行
n：单步执行，不进入函数内部。
s：单步执行，进入函数内部（比如从 main 进入 printList）。
(gdb) n
(gdb) s

print / p：打印变量
(gdb) p node1        # 打印指针地址
(gdb) p node1->value # 打印节点里的值
(gdb) p *node1       # 打印整个节点内容

continue / c 和 quit / q
(gdb) c              # 继续运行到下一个断点或结束
(gdb) q              # 退出 GDB


2. 第二阶段：破案神器（定位 Segmentation fault）
当程序崩溃时，GDB 可以瞬间告诉你死在哪一行、谁调用了它。

gdb ./app
(gdb) r
#程序崩溃，显示 Program received signal SIGSEGV, Segmentation fault.

backtrace / bt：查看调用栈（最重要！）
(gdb) bt
你的终端会输出类似：
#0  main () at main.cpp:27   <-- 罪魁祸首就在第 27 行！
如果是在深层函数里崩溃，bt 
#0  printList (head=0x0) at main.cpp:10
#1  0x000055... in main () at main.cpp:20
这就像监控录像，精确指出是哪一行代码干的坏事。

frame / f：切换栈帧
如果崩溃发生在函数 A 调用的函数 B 里，你想看函数 A 的变量：
(gdb) f 1    # 切换到第 1 层栈帧（main 函数）
(gdb) p node1

info locals：查看当前所有局部变量
(gdb) info locals

3. 第三阶段：进阶追踪（条件断点、监控变量）
条件断点（非常适合循环里排查第 N 次出错）
(gdb) b main.cpp:15 if curr->value == 20   # 当 curr->value 等于 20 时才停下来
(gdb) r

watch：监视变量变化（排查野指针、数据被篡改）
(gdb) b main
(gdb) r
(gdb) watch node1->value   # 当 node1->value 发生改变时，GDB 会立刻停下
(gdb) c

finish：执行完当前函数并停下
(gdb) b printList
(gdb) r
(gdb) finish   # 直接跑完 printList，看它的返回值



# 对目录下的 valgrind_test.cpp (有内存错误) 进行测试
首先
g++ -g -O0 valgrind_test.cpp -o app 
./app
#输出：free(): double free detected in tcache 
#或者 Segmentation fault // 2double free or corruption (fasttop)

1. 第一阶段：基础平A（内存泄漏检测）
这是 Valgrind 最常用、最核心的功能。即使程序没崩溃，它也能告诉你谁没释放内存。

最基础的运行
valgrind ./app
它会输出一大堆，重点看 HEAP SUMMARY 和 LEAK SUMMARY。

完整泄漏检查（强烈推荐）
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./app
--leak-check=full：给出详细的泄漏报告。
--show-leak-kinds=all：显示所有类型的泄漏（包括 still reachable）。
--track-origins=yes：追踪未初始化值的来源（极其有用）。

看懂 LEAK SUMMARY（关键）
终端会输出类似：
LEAK SUMMARY:
   definitely lost: 40 bytes in 1 blocks   <-- 真正的内存泄漏，必须修！
   indirectly lost: 0 bytes in 0 blocks
     possibly lost: 0 bytes in 0 blocks
   still reachable: 12 bytes in 1 blocks   <-- 程序退出时还挂着的（如全局变量），一般可忽略
definitely lost：你 new 了，但指针丢了，永远释放不了。必须修复。
indirectly lost：因为父对象泄漏导致子对象也泄漏。修了父对象，这个一般会跟着好。
still reachable：程序结束时，全局指针还指着它。这通常不是 Bug（比如全局单例），但如果量特别大，也要排查。
在报告中，Valgrind 会精准告诉你泄漏发生在哪一行：
40 bytes in 1 blocks are definitely lost in loss record 1 of 1
   at 0x4C2FB0F: operator new[](unsigned long) (vgpreload_memcheck-amd64-linux.so)
   by 0x4005E6: main (main.cpp:10)   <-- 第 10 行 leaked = new int[10] 漏了！


2. 第二阶段：破案神器（越界与未初始化）
Valgrind 不仅能抓泄漏，还能抓 valgrind ./app 运行时的内存错误。

越界读写（Invalid write / Invalid read）
运行 valgrind ./app，你会看到：
Invalid write of size 4
   at 0x4005F2: main (main.cpp:14)   <-- 第 14 行 arr[10] = 42; 越界了！
 Address 0x... is 20 bytes after a block of size 20 alloc'd
   at 0x...: operator new[](unsigned long)
   by 0x...: createArray() (main.cpp:4)
它直接告诉你越界写发生在第 14 行，并且精准指出这个内存块是第 4 行 new 出来的，大小只有 20 字节（5 个 int），你写了第 20 字节之后的位置。

未初始化值（Conditional jump or move depends on uninitialised value）
Conditional jump or move depends on uninitialised value(s)
   at 0x4006A9: main (main.cpp:18)   <-- 第 18 行 if (uninit[1] == 0) 读了未初始化的值
 Uninitialised value was created by a heap allocation
   at 0x...: operator new[](unsigned long)
   by 0x...: main (main.cpp:17)
它告诉你，第 18 行的判断依赖了一个未初始化的值，这个值是在第 17 行 new int[3] 时分配的，但从未被赋值。

双重释放（Invalid free）
Invalid free() / delete / delete[] / realloc()
   at 0x...: operator delete(void*)
   by 0x4006E1: main (main.cpp:26)   <-- 第 26 行 delete doubleFree; 重复释放
 Address 0x... is 0 bytes inside a block of size 4 free'd
   at 0x...: operator delete(void*)
   by 0x4006D0: main (main.cpp:25)   <-- 第一次释放是在第 25 行


# ldd 查看动态依赖库
当你在 Linux 下运行一个程序时，如果它用了动态链接（绝大多数 C/C++ 程序都这样），系统必须在启动时把依赖的共享库（.so 文件）加载到内存。
ldd 就是用来列出这些依赖，并告诉你它们实际从哪里加载。

ldd ./app
典型输出：
linux-vdso.so.1 (0x00007ffd...)
libstdc++.so.6 => /lib/x86_64-linux-gnu/libstdc++.so.6 (0x00007f...)
libm.so.6 => /lib/x86_64-linux-gnu/libm.so.6 (0x00007f...)
libgcc_s.so.1 => /lib/x86_64-linux-gnu/libgcc_s.so.1 (0x00007f...)
libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f...)
/lib64/ld-linux-x86-64.so.2 (0x00007f...)

<!-->
做了解：
linux-vdso.so.1：内核提供的虚拟动态共享对象，用于加速系统调用，不占磁盘。
libstdc++.so.6：C++ 标准库。
libm.so.6：数学库。
libgcc_s.so.1：GCC 底层运行时库。
libc.so.6：C 标准库（glibc）。
/lib64/ld-linux-x86-64.so.2：动态链接器本身，负责加载所有 .so。
=> 左边是“需要的库名”，右边是“实际加载的路径”，括号里是加载到内存的地址。

典型使用场景
场景一：部署时确认依赖
error while loading shared libraries: libmysqlclient.so.21: cannot open shared object file
用 ldd ./app | grep mysql 一看，发现那一行显示 not found，说明服务器上缺这个库。装上或指定 LD_LIBRARY_PATH 即可。

场景二：查看程序用了哪些第三方库
场景三：排查“库版本冲突”
场景四：检查静态链接
如果程序是静态链接的，ldd 会输出：
not a dynamic executable
说明它不依赖任何 .so，可以独立运行。

常用参数
ldd ./app              # 基本用法
ldd -r ./app           # 显示重定位错误（比如符号找不到）
ldd -u ./app           # 显示未使用的直接依赖（程序没用到但链接了的库）
ldd -v ./app           # 显示版本信息

工作原理与安全警告（面试重点）
ldd 并不是一个简单的“读取文件头”的工具。它本质上是一个 shell 脚本，通过设置环境变量 LD_TRACE_LOADED_OBJECTS=1，然后直接运行目标程序，让动态链接器打印依赖。
这意味着：
如果你对一个不可信的、可能是恶意的可执行文件执行 ldd，它可能会真的运行那个程序，从而触发恶意代码。
所以，安全规范里有一条：不要对来源不明的二进制文件用 ldd。
安全替代方案：
objdump -p ./app | grep NEEDED
readelf -d ./app | grep NEEDED
<-->



# starce 查系统调用
例如 C++ 代码里，fopen 只是一个库函数。但到了操作系统层面，它实际上是发起了一个叫 openat 的系统调用。
strace 就能把这种“语言层面的调用”直接翻译成“操作系统层面的行为”。

<!-->资料了解：
主要用于：
排查“文件找不到”：程序报错“配置文件不存在”，但你看代码路径明明没错。用 strace 一看，原来程序实际去读的是另一个目录。
排查“程序卡死”：服务突然不响应了，CPU 占用不高，也不崩。用 strace 挂上去，发现它卡在 read 或 futex 上，瞬间知道是在等网络数据还是在等锁。
排查“网络连不上”：connect 系统调用返回了什么错误码，一目了然。
性能初步分析：统计哪个系统调用耗时最长、调用最频繁。

查看它到底打开了哪些文件
strace -e trace=openat,read,write,close ./app

输出示例：
openat(AT_FDCWD, "students.txt", O_RDONLY) = 3
read(3, "Alice 18 90\n", 4096) = 12
close(3) = 0
write(1, "Students loaded successfully.\n", 31) = 31

openat 返回 3：文件描述符是 3。
read 返回 12：读到了 12 个字节。
write(1, ...)：1 代表标准输出（终端）。
如果 openat 返回 -1 ENOENT：说明文件根本不存在！程序里写的错误处理立刻就被证实了。

-f：跟踪子进程和线程
你的程序如果用了多线程（std::thread），或者调用了 fork，必须加 -f，否则子线程干了啥你根本看不到。
strace -f ./app

-p <PID>：挂载到正在运行的进程（线上排查神技）
strace -p 12345

-c：统计各系统调用的次数和时间
初步排查性能瓶颈
strace -c ./app
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 45.23    0.001234         123        10           openat
 30.12    0.000821          82        10           read


 -T 和 -tt：显示耗时和精确时间戳
strace -T -tt -e trace=openat ./app

strace：跟踪系统调用（内核层面），比如 open、read、mmap、socket。
ltrace：跟踪库函数调用（用户态层面），比如 strlen、malloc、printf。

致命警告（生产环境大忌）
性能杀手：strace 会让程序慢几十倍到几百倍。因为每次系统调用，内核都要暂停程序，把控制权交给 strace 打印日志，再放行。严禁在生产环境长期开着 strace。
海量日志：一个稍微大点的 C++ 后端服务，几秒钟就能刷出几万行系统调用日志，瞬间把你的磁盘写满。排查完立刻 Ctrl+C 停掉。
权限问题：跟踪别人的进程通常需要 root 权限
<-->