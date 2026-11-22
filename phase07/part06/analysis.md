<!-->
文档解析，供于查看
<-->


T12 是收网的一关——把最后三件“服务框架该干的活”从 main 里彻底赶走：
干什么	T11 里在哪	T12 里在哪
建 listen socket（socket/bind/listen）	main 的 create_and_listen	TcpServer 构造函数
accept 循环 + 建 TcpConnection	main 的 on_accept	TcpServer::handleAccept
维护 conns 连接表	main 的局部 map	TcpServer::conns_
延迟关闭	EventLoop::closeLater(fd)	EventLoop::queueInLoop(fn)


# main._V11.cpp
main 的目标终态：八行。main 里再没有一个 socket、bind、accept、map。


# EventLoop.h

### 为什么 closeLater(fd) 不够用
            closeLater(int fd)	queueInLoop(Functor)
能做什么	只能“延迟关闭这个 fd”	延迟做任何事
载荷	int（4 字节，不会悬空）	一段代码 + 它捕获的一切
谁执行	closeHandler_（固定一个函数）	每段代码自己（千变万化）
扩展性	想延迟干别的事？再加一个 xxxLater	一个原语走天下

closeLater 是 queueInLoop 的特化版本：
// closeLater(5) 用 queueInLoop 表达，就是
loop.queueInLoop([this, fd] { closeConn(fd); });

### 容器形状的变化：vector<int> → vector<Functor>
// T11
std::vector<int>     pendingClose_;      // 一张 fd 名单
for (int fd : pendingClose_) closeHandler_(fd);
// T12
std::vector<Functor> pendingFunctors_;   // 一叠待办纸条
for (auto& f : pendingFunctors_) f();    // 每张纸条自己知道要干什么


# EventLoop.cpp

### doPendingFunctors 为什么必须先 swap（面试题）
void EventLoop::doPendingFunctors_BAD() {
    for (auto& f : pendingFunctors_) {   // ← 直接在成员上遍历
        f();                              // ← 如果 f 里又 queueInLoop(...)
    }
}
执行 f() 的时候，f 里可能又调 queueInLoop(...)，触发 pendingFunctors_.push_back(...)。于是：
问题	                后果
push_back 触发扩容	    底层数组换地址，正在遍历的迭代器失效 → 未定义行为（多半段错误）
即使不扩容	            新塞的 functor 在同一轮就被执行，A 塞 B、B 塞 A → 死循环

正确写法
// ★ 先把整叠"抱走" -> tmp，pendingFunctors_ 变成空
// 在 tmp 上遍历，此时谁往队列里塞都不影响我

### 执行时机：为什么在 handleEvent 之后
这个位置和 T10 的 pendingClose_ 一模一样。这是同一条铁律的第三次出现：
回调执行期间，对象必须活着；延迟动作一律挪到本轮所有回调返回之后。

### 同一个 EPOLLIN，在不同 fd 上是两种完全不同的消息
fd 类型	EPOLLIN 的真实含义	你该干什么	如果你去 read()
listen fd	“accept 队列里有人排队了”	accept()	❌ read() 返回 EINVAL（根本不是数据流）
连接 fd	“接收缓冲区里有数据” 或 “对端 FIN”	read()	✅ 正常
Channel::setReadCallback 里的 “Read” 指的是 EPOLLIN 这个事件位，不是“执行 read 系统调用”。

类比：同一个“叮咚”
        “叮咚” 一响
   ┌────────┼────────┬──────────┐
   ▼        ▼        ▼          ▼
 门铃     手机     微波炉     电梯
 有人来   有消息    热好了    到楼层
 去开门   看消息    去拿碗    出去

// TcpServer::start()
acceptChannel_->setReadCallback([this] { handleAccept(); });   // 门铃 → 开门
// TcpConnection 构造函数
channel_.setReadCallback([this] { handleRead(); });            // 手机 → 看消息

### conn->setMessageCallback(messageCallback_)
完整含义是：
“新来的这个连接，以后收到数据时，用 TcpServer 手里存的那份 process_frames 来处理。”

到底“设定”了什么？
设定的是 main 早先填好的那一份模板。
server.setMessageCallback([](TcpConnection* c, Buffer& in) {
       process_frames(in, c);          // ← 真正的业务
   });

⚠️ 关键：这是值拷贝，不是引用
所以每个连接手里的 std::function 是独立的。将来改动一个，不影响其他

### connectionCallback_ 
是一个**“新连接诞生”的通知**
⚠️ 它在 start() 之前调用“先通知，再开闸” 的好处：业务可以在第一条数据到达之前做好准备（设置选项、初始化上下文）。

### queueInLoop
一句话：把一段代码塞进邮箱，等这一轮活干完了再取出来执行。
特性	说明
立刻返回	塞进去就返回，不执行
执行时机	本轮 for (ch : active_) ch->handleEvent() 全部结束之后
执行者	runOnce 末尾的 <doPendingFunctors()>
捕获规则	只捕获值，不捕获裸指针（除了 this）

queueInLoop 是“延后执行”的原语
T12 里的唯一用法：延迟销毁

### 构造函数
抛异常时，已经构造完成的成员会被自动析构，但 this 的析构函数不会被调用。
所以 listenFd_ 这个裸 int 必须手动 close，否则泄漏。

### 析构函数
为什么必须 close(listenFd_)
listenFd_ 是裸 int，没有 RAII 包装。默认析构函数不会关它。

### start 幂等保护为什么必要
第一次：acceptChannel_ = A，A 注册进 epoll（data.ptr = A）
第二次：acceptChannel_ = B → make_unique 时【先释放 A】→ B 注册
        ↓
        epoll 里还留着 A 的旧注册（data.ptr = 已释放的 A）→ 下次 epoll_wait 返回野指针

### removeConnection()：一行，但含金量
// ① 拷贝成 int
// ② 只捕获值 + this
// ③ 延迟到轮末执行
行	决定	如果做错
①	先取 fd，不用 conn->fd() 延迟取	延迟执行时 conn 可能已经死了 → 悬空指针
②	只捕获 fd（值）+ this	捕获 conn 指针 = 定时炸弹
③	用 queueInLoop 而不是直接 erase	在 handleClose 的栈里析构自己 → UAF




# TcpServer.h

### MessageCallback 为什么要“借”TcpConnection 的？
因为签名只能有一个定义处。
// TcpConnection.h 里（唯一真源）
// TcpServer.h 里（跟着走）
好处：将来 T11 的签名改成 void(TcpConnection*, Buffer*)，TcpServer 一行都不用改

### ConnectionCallback 签名是 void(TcpConnection*)
跟 TcpConnection::ConnCloseCallback 完全一样。但语义不同：
回调	谁填	谁调	含义
MessageCallback	main	TcpConnection::handleRead（间接）	“有数据了”
ConnectionCallback	main	TcpServer::handleAccept	“新连接来了”

### 成员逐个拆
成员	拥有？	为什么是这个类型
loop_	借	EventLoop 属于 main，我只拿指针。协议：main 里必须 EventLoop loop; 声明在 TcpServer 之前
listenFd_	拥有	我在构造函数里 socket() 出来的，我负责 ::close
acceptChannel_	拥有	用 unique_ptr 而不是值 —— 见 3.3
conns_	拥有	同 T11：unique_ptr 保证 Channel 地址稳定（data.ptr 依赖它）
messageCallback_	值	模板：复制给每个新 TcpConnection
connectionCallback_	值	业务通知，TcpServer 自己调

### 为什么 acceptChannel_ 用 unique_ptr 而不是值成员
因为 Channel 的构造需要 (EventLoop*, int fd)，而 fd 是 TcpServer 构造函数里算出来的
// ❌ 想这么写，但 listenFd_ 在初始化列表里还没算出来
TcpServer::TcpServer(EventLoop* loop, uint16_t port)
    : loop_(loop),
      listenFd_(createListenSocket(port)),     // 这一步在列表里
      acceptChannel_(loop, listenFd_)          // ← 这里能用了吗？

教程的方案是延迟到 start()：
acceptChannel_ = std::make_unique<Channel>(loop_, listenFd_);

### 两个私有方法
方法	对应 T11 的什么	干什么
handleAccept()	main 的 on_accept	accept4 循环 → 建 conn → 挂回调 → start() → 入表
removeConnection(TcpConnection*)	main 的 setCloseHandler lambda	只登记，不销毁：loop_->queueInLoop([this, fd]{ conns_.erase(fd); })

为什么 removeConnection 参数是 TcpConnection* 而不是 int fd？
因为它的调用者是 TcpConnection::ConnCloseCallback_，而那个回调的签名是 void(TcpConnection*)

### ~TcpServer 要关 listenFd_
listenFd_ 是裸 int，没有 RAII 封装。默认析构函数不会自动 close 它。
三步顺序：摘 epoll → 关 fd → 析构对象。

### 改 TcpConnection 两处
TcpConnection::~TcpConnection() {
    channel_.disableAll();      // events_=0 → poller 把 fd 从 epoll 摘掉
    ::close(channel_.fd());     // 再关 fd
}

void TcpConnection::handleClose() {
    if (closed_) return;
    closed_ = true;
    channel_.disableAll();        // 停掉所有事件
    if (ConnCloseCallback_) ConnCloseCallback_(this);   // 通知 TcpServer 登记删除
}

### messageCallback_
main 里的 process_frames（函数本体，代码段的固定地址）
        │  setMessageCallback(process_frames)   装
        ▼
TcpServer::messageCallback_        ← 盒子 ①（装着函数指针副本）
        │  handleAccept 里每个新连接都 setMessageCallback(messageCallback_)  复印
        ▼
TcpConnection::messageCallback_    ← 盒子 ②③④…（每个连接各一份，内容相同）
        │  handleRead 里 messageCallback_(this, in_)
        ▼
    真正执行 process_frames



# 用 disableAll() 而不是 removeChannel() 的三个好处
好处	说明
幂等	handleClose() 调一次、~TcpConnection 再调一次， 
        第二次只是 DEL 一个已不在 epoll 里的 fd → ENOENT，容错掉即可。
        不会出现“以为删了其实没删”的状态
不需要 friend	channel_ 是私有的。如果要在 TcpServer 里 
                loop.removeChannel(&conn->channel_)，就得加 friend 或者暴露 channel() getter——都是封装上的裂缝
顺序天然正确	disableAll()（fd 还活着，DEL 成功）→ ::close(fd)。先摘 epoll 再关 fd，正是那条铁律

**************************************************************************
复习
**************************************************************************

# 整条任务链路
T1  裸 socket
T2  一连接一线程
T3  epoll 单线程
T4  全非阻塞 + 写缓冲
T5  切 ET
T6  抽象 Buffer
T7  长度前缀协议
T8  封装 EpollPoller
T9  封装 Channel + 回调分发
T10 封装 EventLoop —— 循环离开 main
T11 封装 TcpConnection —— 连接自己管自己
T12 封装 TcpServer —— main 只剩 8 行


# 最终的层次：
        main                  ← 只有业务：process_frames + 8 行
          │
      TcpServer               ← 框架：socket / accept / 连接表
          │
     TcpConnection            ← 一条连接：读、写、关
          │
        Channel               ← 一个 fd 的监督档案
        │       │
EventLoop   EpollPoller       ← 节奏 + 眼睛
        │
     Buffer                   ← 字节容器
        │
      Linux 内核


# 纯垂直纵深的线性时间轴
###  第一阶段：服务器启动与监听
[ main_V11.cpp ]
  └─> 创建 TcpServer 对象，调用 TcpServer::start()
        │
        └─> [ TcpServer 类 ] 内部执行：socket() → bind() → listen()
              │
              └─> [ TcpServer 类 ] 创建监听 Channel，绑定回调为 TcpServer::handleAccept
                    │
                    └─> [ TcpServer 类 ] 调用 EventLoop::updateChannel()
                          │
                          └─> [ EpollPoller 类 ] 注册 listenfd 的读事件到 epoll

### 第二阶段：新连接建立
[ EventLoop 类 ] 调用 EpollPoller::poll() (底层即 epoll_wait) 阻塞等待...
  │
  └─> [ EpollPoller 类 ] 监听到新连接，epoll_wait 返回
        │
        └─> [ EventLoop 类 ] 遍历活跃 Channel，调用 Channel::handleEvent()
              │
              └─> [ Channel 类 ] 触发读回调（由 TcpServer 注册）
                    │
                    └─> [ TcpServer 类 ] 执行 TcpServer::handleAccept()，拿到 connfd
                          │
                          └─> [ TcpServer 类 ] new TcpConnection 对象，并调用 start()
                                │
                                └─> [ TcpConnection 类 ] 调用 Channel::enableReading()
                                      │
                                      └─> [ EpollPoller 类 ] 将新连接的 connfd 注册进 epoll

### 第三阶段：数据到达与读取
[ EventLoop 类 ] 调用 EpollPoller::poll() (底层即 epoll_wait) 阻塞等待...
  │
  └─> [ EpollPoller 类 ] 监听到可读事件，epoll_wait 返回
        │
        └─> [ EventLoop 类 ] 遍历活跃 Channel，调用 Channel::handleEvent()
              │
              └─> [ Channel 类 ] 触发读回调（由 TcpConnection 注册）
                    │
                    └─> [ TcpConnection 类 ] 执行 handleRead()
                          │
                          └─> [ TcpConnection 类 ] 调用 Buffer::in_.readFd()
                                (将内核数据读到用户态输入缓冲区)

### 第四阶段：业务处理与发送
[ TcpConnection 类 ] 数据读完，触发业务回调（由 main_V11.cpp 注册）
  │
  └─> [ main_V11.cpp ] 执行 process_frames（切包/解包逻辑）
        │
        └─> [ main_V11.cpp ] 业务处理完，调用 TcpConnection::send() 回传数据
              │
              └─> [ TcpConnection 类 ] 调用 Buffer::out_.append()，将数据放入输出缓冲
                    │
                    └─> [ TcpConnection 类 ] 调用 tryFlush() 尝试直接写 socket
                          │
                          └─> (若内核缓冲满未写完) [ TcpConnection 类 ] 调用 Channel::enableWriting()
                                │
                                └─> [ EpollPoller 类 ] 修改 epoll 监听事件，增加 EPOLLOUT

### 第五阶段：断开连接（对端主动关闭）
[ EventLoop 类 ] 调用 EpollPoller::poll() (底层即 epoll_wait) 阻塞等待...
  │
  └─> [ EpollPoller 类 ] 监听到对端 FIN，epoll_wait 返回
        │
        └─> [ EventLoop 类 ] 遍历活跃 Channel，调用 Channel::handleEvent()
              │
              └─> [ Channel 类 ] 触发读回调
                    │
                    └─> [ TcpConnection 类 ] 执行 handleRead()，此时 readFd() 返回 0
                          │
                          └─> [ TcpConnection 类 ] 得知关闭，执行 handleClose()
                                │
                                └─> [ TcpConnection 类 ] 调用 Channel::disableAll() 注销所有事件
                                      │
                                      └─> [ TcpConnection 类 ] 触发 closeCallback（由 TcpServer 注册）

### 第六阶段：资源销毁
[ TcpServer 类 ] 在 closeCallback 中，调用 EventLoop::queueInLoop() 将销毁任务抛给 IO 线程
  │
  └─> [ EventLoop 类 ] 在下一轮 loop 循环中，执行 doPendingFunctors()
        │
        └─> [ TcpServer 类 ] 执行排队的回调：从内部 map 中 erase 该连接
              │
              └─> [ 系统(C++) ] TcpConnection 的 shared_ptr 引用计数归零
                    │
                    └─> [ TcpConnection 类 ] 自动执行析构函数 ~TcpConnection()
                          (彻底死亡，释放内存) 