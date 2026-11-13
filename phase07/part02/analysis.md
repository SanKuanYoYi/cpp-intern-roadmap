<!-->
文档解析，供于查看
<-->

# Buffer.h：三个 index 撑起一切
V4 的 Conn.out      ←→   V5 的 Buffer out
out.data() + sent      ←→   peek()
out.size() - sent      ←→   readableBytes()
out.append(...)        ←→   append(...)
out.clear()            ←→   retrieveAll()
out = out.substr(...)  ←→   retrieve(n)
read(fd, buf, ...)     ←→   readFd(fd, &err)


 buf_ (vector<char>，总容量 capacity)
 ┌─────────────┬──────────────────┬──────────────────┐
 │ prependable │    readable      │    writable      │
 │  (可插入头)  │  (可读未取走)     │   (可写入的空位)  │
 └─────────────┴──────────────────┴──────────────────┘
 0          readerIndex_       writerIndex_      capacity
            └─ 已取走/可复用 ─┘

量	计算方式	含义
readableBytes()	writerIndex_ - readerIndex_	还没被 retrieve 取走的数据长度
writableBytes()	buf_.size() - writerIndex_	尾部还有多少空位可写
prependableBytes()	readerIndex_	头部空余（readerIndex_ 之前的空间，理论上可复用）

读和写是两条独立的游标，readerIndex_ 追 writerIndex_。V4 里 sent 和 out.size() 的正式版


### const char* peek() const; 
为什么不是返回 std::string？ 避免拷贝。peek() 给你指针，调用方配合 readableBytes() 用
write(fd, c.out.peek(), c.out.readableBytes());
//          ↑ 零拷贝，直接指进 Buffer 内部
V4 里 c.out.data() + c.sent 是同一个东西，只是封装了偏移计算。


### void retrieve(size_t len);  void retrieveAll();             
关键语义：retrieve 不删数据，只挪游标。
为什么不真删？ 因为 vector 头部删除是 O(n) 拷贝。挪游标是 O(1)。
等空间真不够时，ensureWritableBytes 再统一 compact
（把可读数据 memmove 到 kCheapPrepend 位置）——把多次小开销合并成一次大整理。


### ssize_t readFd(int fd, int* savedErrno);
返回值语义和 read 对齐：
.>0	读到 n 字节（已在 Buffer 里）
0	对端关闭（FIN）
-1	出错，errno 存进 *savedErrno

为什么不用普通 read？ 看它内部的 readv 设计

一块 read 只能读进"当前 writableBytes()"那么多
如果对端发来 100KB，而 Buffer 只剩 4KB 可写
    → 你要么先扩容（浪费），要么多次 read（多次 syscall）

readFd 用 readv 一次读两块：
    vec[0] = 现有可写空间（比如 4KB）
    vec[1] = 栈上 64KB 备用区
    → 一次 syscall 最多读 68KB
    → 超出 writableBytes 的部分 append 到 Buffer 里

好处：大包到达时减少扩容次数和 syscall 次数；备用区在栈上，不占堆。


### kCheapPrepend = 8 为什么是 8？
为将来"在数据前面塞协议头"预留空间。
如果要加长度前缀协议：[4 字节长度][payload]。
收到 payload 后如果要做某些处理需要在 payload 前面插 4 字节头
8 = 4 字节长度 + 4 字节其他（如魔数/类型），够用且不浪费


### ensureWritableBytes 
ensureWritableBytes 是 Buffer 的核心私有逻辑
① writableBytes() >= len          → 直接返回
② prependable + writable >= len + kCheapPrepend
   → memmove 可读数据到 begin()+kCheapPrepend
   → 更新 readerIndex_ 和 writerIndex_（不扩容）
③ 还不够                          → buf_.resize(writerIndex_ + len)（扩容）



# buffer_test.cpp
测试不只是"能跑"，它用断言定义了 Buffer 应该满足的所有性质。

### append 到"取走后"的 Buffer
append 不覆盖可读区。
操作前：[h e l l o | writable]
              ↑reader=2  ↑writer=5
append "world" 后：
        [h e l l o w o r l d | writable]
              ↑reader=2        ↑writer=10
        peek() 从 index 2 开始 → "lloworld"
它守护了"读写游标分离"这个核心设计。


### readFd 小包
（走 vec[0]）
小数据（≤ writableBytes）走 vec[0]，直接读进 Buffer 内部，不经过 extrabuf

### readFd 大包
（走 vec[1] + append）
small 只有 16 字节 writable
readv 一次读两块：
    vec[0] = 16 字节（填满 small 现有空间）
    vec[1] = extrabuf[0..84]（栈上 64KB 备用区，装剩下 84 字节）
readv 返回 100
实现里：
    n(100) > writableBytes(16)
    → 先 writerIndex_ += 16（填满 vec[0]）
    → 再 append(extrabuf, 100 - 16 = 84)（把备用区的塞进 small）
    → small.readableBytes() == 100 ✅
先 write(fds[1], payload) 再 readFd——管道容量 64KB
它守护了"readFd 一次 syscall 尽量多读"这个契约。


# main_V5.cpp
把你里 Conn 中的 std::string out + size_t sent 这对"裸组合"，抽成一个独立、可单测、可复用的 Buffer 类。
功能不变，但接口更清晰，且为 T7+ 的"长度前缀协议切包"打地基。
