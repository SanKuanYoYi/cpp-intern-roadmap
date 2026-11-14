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


# main_V6.cpp
V6 到底在解决什么？——用一句话说清
TCP 是字节流，不保证"send 一次 = recv 一次"。 你发 3 个帧，对端可能：
一次 read 到 3 个帧（粘包）
分 3 次读到，每次半帧（半包）
混着来（第 1 次读到 1.5 帧，第 2 次读到 1.5 帧）

这不是 bug，是设计。 应用层必须自己"划界"。
V6 用长度前缀划界：[4 字节 len 大端][payload len 字节]。


### 必须把 in 和 out 拆开—— V5 的单 Buffer 混不过去了
struct Conn {
    int fd = -1;
    Buffer in;          // 收：内核 → 用户态，可能只有半个帧
    Buffer out;         // 发：待发送队列
    bool want_write = false;
};
V5的 echo 能用一个 Buffer 混过去，因为"读进来什么就原样写出去"。V6 不行：
in:  可能永久留着半个帧，等后续数据
out: 已经确定要发、还没发完的数据
混在一起 → 你分不清"没发出去"和"没凑齐"


### process_frames 五条规则 + while 为什么是灵魂
规则	判断	返回	含义
①	in.readableBytes() < 4	true	头都没凑齐，回去等
②	读头后 len > 10MB	false	恶意/损坏，断开
③	in.readableBytes() < 4 + len	true	body 没凑齐，等
④	凑齐了	继续处理	retrieve(4) 吃头 → 拷 len 字节到 out → retrieve(len) 吃 body
⑤	整个包在 while (true) 里	—	直到某次 return true

规则⑤：while 不是 if
一次 read 里来了 3 个完整帧
    │
    ├─ if 写法：只处理第一个帧，剩下 2 个留在 in 里
    │       ↓
    │   ET 模式下"in 里还有数据但没新数据到达" → 不再触发 EPOLLIN
    │       ↓
    │   剩下 2 帧永远卡在 in 里 → 客户端永远等不到响应
    │       ↓
    │   这就是"粘包处理错误"的典型事故
    │
    └─ while 写法：循环处理，帧 1 → 帧 2 → 帧 3 → 没有完整帧 → return true
            ↓
        全部处理完，in 里只剩"半个帧"或"空"

### if (in.readableBytes() < 4) return true;
场景：客户端只发了 f[:2]（2 字节头），in 里只有 2 字节。连头都不够
为什么 return true 不是 false？
true = "数据不够，正常，回去等" → 不断连
false = "协议错误" → 断连

### 读 4 字节大端长度
假设客户端要发一个 len = 1000 的帧。1000 的十六进制是 0x000003E8。
大端序（网络字节序）下，这 4 个字节在内存里是：
内存地址：    +0    +1    +2    +3
字节值：     0x00  0x00  0x03  0xE8
             ↑
             in.peek() 指向这里
内存里存的是 4 个独立的字节，不是 1 个 32 位数。
CPU 不知道这 4 个字节应该被当成一个整数——是你（应用层）约定"这 4 个字节拼起来是一个大端长度"。

reinterpret_cast<const unsigned char*> 干了什么？
它不改变内存里的任何数据，只是告诉编译器："接下来按无符号字节来读这 4 个字节"。
char 在 Linux/macOS 上默认是有符号的（范围 -128~127）
这一步不是"可选的优化"，是"必须的正确性保障"：不转，0xE8 会因为符号扩展把高位污染成全 1，长度算成天文数字。

大端序的约定是：先到的字节是高位，后到的是低位。
字节序列：    [p[0]]  [p[1]]  [p[2]]  [p[3]]
              0x00    0x00    0x03    0xE8
分别移到 32 位的对应位置：
    p[0] 是最高的 8 位   → 左移 24 位
    p[1] 是次高的 8 位   → 左移 16 位
    p[2] 是再次的 8 位   → 左移 8  位
    p[3] 是最低的 8 位   → 不移
    位置：  [31..24] [23..16] [15..8]  [7..0]
    来自：  p[0]     p[1]     p[2]     p[3]

(uint32_t)p[0] << 24   = 0x00 << 24 = 0x00000000
(uint32_t)p[1] << 16   = 0x00 << 16 = 0x00000000
(uint32_t)p[2] << 8    = 0x03 << 8  = 0x00000300
(uint32_t)p[3]         = 0xE8       = 0x000000E8
4 个值按位或（|）拼起来：
  0x00000000
| 0x00000000
| 0x00000300
| 0x000000E8
-------------
  0x000003E8   = 1000  ✅

换个更直观的例子：len = 0x12345678
字节序列：  p[0]  p[1]  p[2]  p[3]
            0x12  0x34  0x56  0x78
计算：
    (uint32_t)0x12 << 24  = 0x12000000
    (uint32_t)0x34 << 16  = 0x00340000
    (uint32_t)0x56 << 8   = 0x00005600
    (uint32_t)0x78        = 0x00000078
按位或：
    0x12000000
  | 0x00340000
  | 0x00005600
  | 0x00000078
  ------------
    0x12345678   ✅

网络字节序统一规定是大端

### if (len > kMaxFrame) return false;
场景：客户端发 struct.pack(">I", 0xFFFFFFFF)（只发 4 字节头，len=4GB）。
先判超长再判 body 齐不齐，顺序反了会 OOM


### 核心：retrieve(4) → 写头 → append body → retrieve(len)
处理前：
in:  [L0 L1 L2 L3 | B0 B1 B2 ... B(len-1) | 后续帧...]
     ↑peek()                      
     readerIndex_
第 1 步：in.retrieve(4)
in:  [L0 L1 L2 L3 | B0 B1 B2 ... B(len-1) | 后续帧...]
                  ↑peek()（readerIndex_ 前进 4，指向 B0）
                  ↑readerIndex_
第 2 步：out.append(hdr, 4)
out: [L0 L1 L2 L3]   ← 头写进 out
第 3 步：out.append(in.peek(), len)
out: [L0 L1 L2 L3 | B0 B1 B2 ... B(len-1)]
第 4 步：in.retrieve(len)
in:  [L0 L1 L2 L3 | B0 B1 B2 ... B(len-1) | 后续帧...]
                                          ↑peek()（readerIndex_ 前进 len）
                                          ↑readerIndex_
      ↑ 数据还在 in 里，只是"看不见"了（等 makeSpace compact 才回收）

retrieve(4) 必须在 peek() 取 body 之前
peek() 永远返回 begin() + readerIndex_。retrieve(4) 让 readerIndex_ 前进 4，peek() 自然指向 body 起点。

### while (true) 是灵魂
一次 readFd 可能读进 3 个完整帧：
in: [帧1][帧2][帧3]
while 循环：
    迭代 1：处理帧 1 → out 多了 4+len1
    迭代 2：处理帧 2 → out 多了 4+len2
    迭代 3：处理帧 3 → out 多了 4+len3
    迭代 4：in.readableBytes() < 4 → return true
如果写成 if：
    只处理帧 1，帧 2、帧 3 留在 in 里
    → ET 模式下没新数据就不通知 → 帧 2、帧 3 永远卡死
    
#### 一张图看完整数据流
客户端发送：[帧1][帧2][帧3]  （一次 sendall，TCP 可能一次全到）
                │
                ▼
server readFd → c.in
in: [帧1][帧2][帧3]
                │
                ▼
process_frames(c.in, c.out)  ← while 循环
    ├─ 迭代1：len=len1，够 → retrieve(4) → out+=hdr1 → out+=body1 → retrieve(len1)
    ├─ 迭代2：len=len2，够 → retrieve(4) → out+=hdr2 → out+=body2 → retrieve(len2)
    ├─ 迭代3：len=len3，够 → retrieve(4) → out+=hdr3 → out+=body3 → retrieve(len3)
    └─ 迭代4：in.readableBytes() < 4 → return true
                │
                ▼
out: [帧1][帧2][帧3]  （3 个完整帧，顺序和客户端发的一致）
                │
                ▼
try_flush(c) → write 全部发给客户端




### 三个关键易错点
①：操作顺序——retrieve(4) 必须在 peek() 取 body 之前
②：len == 0 的边界
回一个空帧 ✅
③："头齐" ≠ "帧齐"


### 读循环改成"读 → 切包 → flush"
① readFd 读进的是 c.in，不是 c.out
T6 的 echo 读进 out，因为"读进来直接发出去"。T7 读进 in，因为要先切包、再决定回什么。
② 每次 readFd 成功后都 
不立刻 flush，数据会全堆在 out 里——虽然最后也会发，但延迟增大。每次读后立刻发，让客户端"感觉"响应快。try_flush 就是 T6 的 flush，改名强调"能发就发，发不完自动注册 EPOLLOUT"。
③ r > 0 时 continue，不是 break


### 补上 memmove 分支 (Buffer.cpp)
ensureWritableBytes 只有 resize 的后果：前端被读走的空洞永远不复用，缓冲区只涨不缩。

