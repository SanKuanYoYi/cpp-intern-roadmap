#gdb基础理解
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


------------------------------------------------------------------------------------
#Valgrind初识
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
#以下为随机场景命令使用练习#
************************************************************************************

#查看当前系统所有监听端口，并找出 3306 是否在监听（MySQL 默认端口）
① ss -tuln | grep 3306
② netstat -tuln | grep 3306
③ lsof -i | grep 3306


#
