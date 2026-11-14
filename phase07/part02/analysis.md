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

"可读区/可写区"的本质：三个区段的逻辑约定。这是纪律，不是硬件。
  [0, readerIndex_)          prependable  可插入头部（T7 协议头用）
  [readerIndex_, writerIndex_)  readable   可被 peek/retrieve 消费
  [writerIndex_, buf_.size())   writable   可被 append 写入


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


### makeSpace 的三种情况
1. 情况①：writableBytes() >= len（最常见的快路径）
[8B|  abc  |        writable 100        ]
         ↑r=11    ↑w=14
append(50)：writable=100 >= 50 → 直接在 w 处写，w += 50

2. 情况②：writable 不够，但 prependable + writable 够（compact）
场景：buf_=1032，已读走一大段，readerIndex 涨到 1000，只剩 32 字节 writable

改前：[8B|  (已取走 992B，废弃)  | abcde | 32B writable ]
           ↑r=1000                 ↑w=1005

append(100)：writable=32 < 100 → 进 makeSpace
    prependable + writable = 1000 + 32 = 1032
    len + kCheapPrepend = 100 + 8 = 108
    1032 >= 108 → 情况②，压缩！
memmove 可读数据到 begin()+8：

改后：[8B| abcde | writable ~1019 ]
           ↑r=8  ↑w=13
→ 现在 writable = 1032 - 13 = 1019 >= 100 ✅
→ 不扩容！把"废弃的 992 字节"回收利用了

readerIndex_ 之前的空间（已取走的废弃区）没有浪费，只是暂时"不可见"，compact 时能全部回收。 
这就是"延迟整理"的收益——多次 retrieve 不搬，一次 append 需要时才统一搬。

3. 情况③：prependable + writable 也不够（扩容）
场景：buf_=1032，readerIndex=8，writerIndex=1032（全写满了，还没读）
      prependable=8，writable=0

append(1000)：
    writable+prependable = 8 < 1000 + 8 = 1008 → 情况③

buf_.resize(writerIndex_ + len) = resize(1032 + 1000) = resize(2032)
改后：[8B| 已填满的数据（1032-8=1024B） | writable 1000 ]
→ 内容还在原位，只是尾部多了 1000 字节空间

resize 的参数：writerIndex_ + len，不是 buf_.size() + len。
因为 writerIndex_ 才是"已用到的真实位置"，尾部如果有废弃区（readerIndex 涨了但没 compact），writerIndex_ + len 会重新覆盖掉


### readFd(fd, savedErrno) —— 本关最精华
目标：一次 syscall 尽量多读，减少扩容，减少 read 调用。

1. struct iovec {
    void  *iov_base;   // 内存块起始地址
    size_t iov_len;    // 这块内存能读/写多少字节
};

配合 readv：一次 syscall 读进多块内存
普通 read：
ssize_t read(int fd, void *buf, size_t count);
//                   ↑ 只能一块连续内存
readv：
ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
//                    ↑ 一个 iovec 数组        ↑ 数组里几个元素
语义：内核从 fd 读数据，按 iov 数组顺序，先填满 iov[0]，再填 iov[1]，再 iov[2]……直到总容量填满或没数据了。

readv(fd, vec, 2) 的物理过程：
fd 里的数据流：  [ a b c d e f g h i j ... ]
vec[0] = {addr_A, 4}   → 内核往 addr_A 写 a b c d
vec[1] = {addr_B, 6}   → 内核往 addr_B 写 e f g h i j
总容量 = 4 + 6 = 10，一次 syscall 最多读 10 字节
返回 10（或不足时返回实际读到的字节数）

关键收益：addr_A 和 addr_B 可以不连续（一块在堆、一块在栈），但一次 syscall 就都填好了。
普通 read 做不到——你只能一次读一块，要读两块就两次 syscall。

① iov_len 是"我允许你写多少"，不是"实际写了多少"
返回值 n 才是实际读到的总字节数。 
判断"vec[0] 有没有被填满"靠 n 和 vec[0].iov_len 比较，不是读 iov_len 本身。
② iovcnt 有上限（IOV_MAX，通常 1024）
③ iov_base 是 void*，readv 写、writev 读

2. 为什么用 const size_t writable = writableBytes() 先存起来？
因为 append 会改变 writerIndex_，如果中途再调 writableBytes() 拿到的是新值，判断 n <= writable 就错了

3. 为什么 iovcnt 要判断？
如果 Buffer 本身就有 ≥ 64KB 可写空间，extrabuf 根本用不上（vec[0] 一块就装得下），传 2 块反而让内核多做判断



# main_V5.cpp
把你里 Conn 中的 std::string out + size_t sent 这对"裸组合"，抽成一个独立、可单测、可复用的 Buffer 类。
功能不变，但接口更清晰，且为 V6+ 的"长度前缀协议切包"打地基。

1. Conn 结构：sent 彻底消失
Buffer out; // 替代 std::string out + size_t sent
2. flush 改写：retrieve 替代 sent +=
3. read 循环改写：readFd 一行顶 V4 五行

### 为什么 readFd 比 read 快这么多？
看 readFd 的 readv：vec[0] 是 Buffer 内部可写区，vec[1] 是栈上 64KB。
一次 syscall 最多读 writable + 65536 字节。
第一次：writable≈1KB，读约 66KB
之后 Buffer 扩容/compact，每次仍能读约 64KB
1MB 数据约 16 次 readFd，16 次 readv syscall
对比 T4：256 次 read syscall


