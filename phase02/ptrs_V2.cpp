/*
自测练习使用。无其他用途
*/

#include <iostream>
#include <memory>
#include <string>

struct Node {
    std::string name;
    std::shared_ptr<Node> next;
    std::shared_ptr<Node> prev; // 破坏实验：weak_ptr 改回 shared_ptr

    explicit Node(std::string n) : name(std::move(n)) {}

    ~Node() {
        std::cout << "~Node " << name << '\n';
    }
};



int main() {
    {
        auto a = std::make_shared<Node>("A");
        auto b = std::make_shared<Node>("B");

        a->next = b;
        b->prev = a;

        std::cout << "a.use_count() = " << a.use_count() << '\n';
        std::cout << "b.use_count() = " << b.use_count() << '\n';
    }

    std::cout << "after scope\n";
    return 0;
}

/*
输出结果：
a.use_count() = 2
b.use_count() = 2
after scope

注意：没有输出：
~Node A
~Node B
*/

/*
原因：
- `a->next = b;` 使 `b` 的强引用计数 +1。
- `b->prev = a;` 使 `a` 的强引用计数 +1。
- 离开作用域后，局部变量 `a` 和 `b` 销毁，但：
  - `Node A` 仍被 `b->prev` 强引用。
  - `Node B` 仍被 `a->next` 强引用。
- 两者强引用计数都不为 0，形成循环引用，堆对象无法释放。
*/

/*
valgrind 输出一大堆
AI 解析关键部分：

==8079== HEAP SUMMARY:
==8079==     in use at exit: 160 bytes in 2 blocks
==8079==   total heap usage: 4 allocs, 2 frees, 74,912 bytes allocated
程序退出时，还有 160 字节，2 个块没释放。
整个程序总共做了 4 次分配，只释放了 2 次。
这 160 字节就是两个 Node 对象（make_shared 一次性分配对象 + 控制块，每个大约 80 字节）。

==8079== 80 bytes in 1 blocks are indirectly lost in loss record 1 of 2
==8079==    at 0x4853FD3: operator new(unsigned long)
...
==8079==    by 0x4002BCA: std::make_shared<Node, char const (&) [2]>
==8079==    by 0x4002350: main (ptrs_V2.cpp:26)
...
main (ptrs_V2.cpp:26)
这 80 字节是 Node B。
它被称为 indirectly lost（间接丢失），因为它不是被局部变量直接持有而丢失，
而是被另一个已经泄漏的对象（Node A）的成员 next 强引用着，所以跟着一起泄漏了。

==8079== 160 (80 direct, 80 indirect) bytes in 1 blocks are definitely lost in loss record 2 of 2
==8079==    at 0x4853FD3: operator new(unsigned long)
...
==8079==    by 0x4002BCA: std::make_shared<Node, char const (&) [2]>
==8079==    by 0x400233A: main (ptrs_V2.cpp:25)
...
main (ptrs_V2.cpp:25)
这 160 字节 = 80 直接丢失（Node A 本身）+ 80 间接丢失（Node B）。
Node A 是 definitely lost（确定丢失）：
因为局部变量 a 销毁后，没有任何外部指针能再找到它，但它还被 b->prev 强引用着，所以无法释放。
Node B 被算作 Node A 的间接丢失：
因为 Node B 只被 Node A->next 引用，Node A 泄漏了，Node B 也就跟着泄漏。
所以两条记录加起来，实际就是两个 Node，共 160 字节。

==8079== LEAK SUMMARY:
==8079==    definitely lost: 80 bytes in 1 blocks
==8079==    indirectly lost: 80 bytes in 1 blocks
==8079==      possibly lost: 0 bytes in 0 blocks
==8079==    still reachable: 0 bytes in 0 blocks
==8079==         suppressed: 0 bytes in 0 blocks
efinitely lost: 80：Node A 确定丢了。
indirectly lost: 80：Node B 被 Node A 拖着一起丢了。
总共 160 字节泄漏。
没有 still reachable，因为程序退出时没有任何全局或静态指针还能找到它们。

==8079== ERROR SUMMARY: 1 errors from 1 contexts (suppressed: 0 from 0)
只有 1 个错误上下文，就是内存泄漏。
没有 Invalid read/write，没有 Invalid free，因为程序没崩溃，只是安静地漏了内存。
*/