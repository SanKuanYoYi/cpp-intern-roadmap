<!-->
文档解析，供于查看
<-->


# T14 主从 Reactor 的解法
这关在干什么（大白话）
现在是一个员工干所有活：
main 线程：接客 → 记本子 → 算业务 → 端菜 → 送客 → 下一桌

1000 个连接时，所有 read / write / 你的 process_frames 都在这一个线程里排队。
哪怕机器 8 个核，你也只用掉了 1 个。

### 主从 Reactor 的解法
再招 4 个员工，并定一条铁律：
main 线程（前台迎宾）            服务员 1
只管 accept   ──round-robin──▶ 顾客 A、D 的一切（读、写、关闭）
                                服务员 2
                                顾客 B、E 的一切
铁律：一位顾客从进门到离开，只由同一个服务员负责。

方案	连接会被几个线程碰	需要锁吗
单线程	1 个	不需要（本来就一个人）
多线程 + 连接共享	N 个（谁空谁处理）	每个 Buffer、每个状态位都要加锁
主从 Reactor（本关）	1 个（固定）	连接内部完全不需要锁


# EventLoopThreadPool

它不干活。它只是个排班表 + 叫号机：
它做的事	类比
持有 N 个 EventLoopThread	名册上登记了 N 个服务员
启动它们	让他们各自上岗（startLoop()）
getNextLoop()	叫号机：“下一位 → 2 号服务员”

IO 逻辑一行都没有，全在 TcpConnection 和 EventLoop 


### 六个成员
1.	成员	类型	职责	所有权
①	baseLoop_	EventLoop*	主 loop（跑 accept 的那个）。退化时直接返回它	借（不拥有）
②	numThreads_	int	要开几个 sub loop（不含 base！）	值
③	started_	bool	幂等标记（教程定义了但没用上，见第五节）	值
④	next_	size_t	round-robin 游标，指向上次分给了谁	值
⑤	threads_	vector<unique_ptr<EventLoopThread>>	线程本身，负责它们的生死（join）	拥有
⑥	loops_	vector<EventLoop*>	每个线程里的 loop 指针，供 getNextLoop 取	借（指向子线程栈对象）

2. ⑤ 与 ⑥ 为什么要分成两个 vector
EventLoop 是 EventLoopThread::threadFunc() 里的栈对象，池子既没创建它、也不能删它——它由子线程的栈帧管生死。

            用 unique_ptr 会怎样
threads_	✅ 正确：线程确实归池子管，必须 join
loops_	    ❌ 说谎：读者会以为“池子析构时会 delete”，实际 EventLoop 在子线程栈上

### numThreads_ 的语义（容易误解）
numThreads 是 sub loop 的数量，不含 base

### 析构函数
// 默认析构即可：
//   threads_ 析构 → 每个 EventLoopThread 析构 → quit() + join()
//   子线程退出后，其栈上的 EventLoop 才析构；此时 loops_ 里的指针全部失效，
//   但 loops_ 已先于此步被清空（它只存指针），所以安全。

### start()
点	说明
numThreads_ <= 0 退化	
不建线程，loops_ = {baseLoop_}。这样单线程模式和主从模式共用同一套代码——TcpServer 里不用写 if。这是“降级优雅”

startLoop() 会阻塞	它内部 cond_.wait 等子线程把 EventLoop 建好。所以 start() 返回时，所有 sub loop 都真的在跑了

push_back(std::move(t))	t 是 unique_ptr，move 进 vector 后 t 为空。所有权转交，start() 结束时不析构线程

注意先存 loops_ 再存 threads_：如果先 move 进 threads_，t 就空了，没法再调 t->startLoop()。

if (started_) return;      // ← 幂等
如果 start() 被调两次
第 1 次：4 个线程，threads_ = [t1..t4]，loops_ = [l1..l4]
第 2 次：threads_.clear()? → 没有 clear，直接 push
         → threads_ = [t1..t4, t5..t8]，loops_ = [l1..l8]
         → 线程数翻倍（资源浪费）
如果第二次执行时 threads_ 被赋值覆盖，旧线程会在 unique_ptr 析构时 join——而它们的 loop 还在跑，join 卡死。

### getNextLoop()
点	说明
loops_.empty() 兜底	start() 之前调也能跑（返回 base loop）。让“忘调 start”变成“退化单线程”而不是“段错误”

% size 环形	next_ 到末尾自动回到 0。用 size_t 而非 int，% 天然非负

不加锁	⚠️ 只在 base loop 线程调用（handleAccept 里）。这是“单写者”保证，不是“它天生安全”——如果你在 sub 线程调它，next_++ 就是数据竞争

### 联系
EventLoopThreadPool::start()
  └─ EventLoopThread::startLoop()      ← T13 的东西
       ├─ thread_ = std::thread(threadFunc)
       ├─ threadFunc: EventLoop loop;  （子线程栈上）
       ├─ cond_ 等 loop_ 就绪
       └─ return loop_                 ← 存进 loops_



# TcpServer 的改动

### 总览
    改哪里	改动	影响面
1	构造函数签名	加 int numThreads = 4	调用方兼容（默认参数）
2	成员列表	加 unique_ptr<EventLoopThreadPool>，插在 acceptChannel_ 和 conns_ 之间	决定析构顺序
3	start()	第一句加 threadPool_->start()	启动顺序
4	handleAccept()	3 行：getNextLoop / 传给 TcpConnection / start() 改走 runInLoop	全关核心
5	removeConnection()	不改代码，只加注释	不变量声明

### 构造函数：多一个参数，初始化列表加一项
点	说明
默认参数 = 4	调用方 new TcpServer(&loop, 8080) 不用改；想调也在那一行

threadPool_ 放进初始化列表	必须在 loop_ 之后初始化（声明顺序决定），否则传进去的是未初始化的指针

### 成员顺序：决定“谁先死”
private:
    EventLoop* loop_;                                     // 借
    int listenFd_ = -1;                                   // 拥有（裸 fd，手动 close）
    std::unique_ptr<Channel> acceptChannel_;              // 拥有
    std::unique_ptr<EventLoopThreadPool> threadPool_;     // ← 新增，必须在 conns_ 之前
    std::map<int, std::unique_ptr<TcpConnection>> conns_; // 拥有
    MessageCallback    messageCallback_;
    ConnectionCallback connectionCallback_;

实际销毁顺序
connectionCallback_ → messageCallback_
    → conns_（每个 TcpConnection 析构）
        → 每个 conn 的 ~TcpConnection：channel_.disableAll() → close fd
    → threadPool_（每个 EventLoopThread 析构）
        → quit() + join() → 子线程退出 → 子线程栈上的 EventLoop 析构
    → acceptChannel_ → listenFd_ → loop_

### 为什么 conns_ 必须在 threadPool_ 之前死？
因为 TcpConnection 里的 channel_ 注册在sub loop 的 epoll 上。

如果 threadPool_ 先死：
threadPool_ 析构
  → EventLoopThread::quit() + join()
  → 子线程退出，子线程栈上的 EventLoop 析构
  → EpollPoller 析构，epoll fd 关闭
然后 conns_ 析构
  → ~TcpConnection → channel_.disableAll()
  → Channel::update() → poller_.remove()   ← poller_ 已经销毁了！
  → 💥 UAF

### start()：先起线程，再开闸
为什么 threadPool_->start() 必须在 enableReading() 之前？
enableReading() 之后，listenFd 随时可能可读 → handleAccept 随时可能被调用 → 里面要 getNextLoop()。
如果线程池还没 start，loops_ 是空的 → 退化成返回 baseLoop_ → 所有连接都跑在 base 线程，T14 等于白做。

### handleAccept()：只改 3 行，但这是全关核心
行	内容	为什么
①	getNextLoop()	round-robin 挑一个 sub loop。只在 base 线程调（next_ 无锁）
②	TcpConnection(ioLoop, fd)	这是最容易漏的一行：漏了还是传 loop_ → 所有连接仍然注册在 base loop 的 epoll 上 → 线程开了但一个连接都没分过去
③	conns_[fd] = move(conn)	T12 的决策：先入库再启动，防回调里 forceClose 后给已关连接重订阅
④	ioLoop->runInLoop([raw]{ raw->start(); })	全关最重要的一行，下一节专门讲
⑤	connectionCallback_(raw)	我调整到最后（教程是放在入库前），理由见下面的“顺序坑”

顺序坑
如果顺序是 回调 → 入库 → 启动。如果回调里 forceClose()：
handleClose → closeCallback → removeConnection → queueInLoop(erase fd)
   ↑ 此刻 conns_ 里【还没有 fd】→ erase 是空操作
落库：conns_[fd] = 已关闭的 conn
   ↑ 它永远不会被 erase（closeCallback 只触发过一次）
   ↑ conn 对象 + fd 双泄漏

调整成 入库 → 启动 → 回调 就安全了：
若回调里 forceClose	后果
入库后 erase 能找到 fd	✅ 真正删掉，无泄漏
start() 已投递但尚未执行	执行时 start() 里检查 closed_ → 直接 return（T12 加的检查在这里救命）

### 为什么 start() 必须走 runInLoop
一个线程在 epoll_wait 时，另一个线程可以 epoll_ctl 往同一个 epoll 实例加 fd。
真正不安全的是 Channel
class Channel {
    EventLoop* loop_;
    int  fd_;
    int  events_;      // ← 没有 mutex 保护
    int  revents_;     // ← 没有 mutex 保护
    int  index_;       // ← 没有 mutex 保护
};

如果 base 线程直接调 conn->start()：
base 线程：channel_.enableReading() → channel_.update() → 改 events_
io 线程：  channel_.handleEvent()  → 正在读/写 revents_
                    ↑
              同一个对象，两个线程，无人加锁 → 数据竞争（UB）



### removeConnection()：代码一行不改，但必须加注释
// conns_ 只在 base loop 线程增删：
    //   + 插入：handleAccept（base 线程）
    //   - 删除：此处排队回 base 线程
    // 因此 conns_ 不需要 mutex。
这就是主从 Reactor 省掉锁的方式：不是“锁写得少”，而是用排队把所有的写操作集中到一个线程。
conns_ 的所有权归 base loop 线程；其他线程只能通过 queueInLoop 请求修改。

