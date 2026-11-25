<!-->
文档解析，供于查看
<-->

# T13｜EventLoopThread + eventfd 唤醒

### 这一关要解决的核心问题
现在 epoll_wait(-1) 卡在那里时，除了“有 IO 事件”，没有人叫得醒它。

代码	            现实
EventLoop	      值班经理，坐在监控室
epoll_wait(-1)	经理睡着了（-1 = 无限期睡，不是“眯一会儿看看”）
别的线程	      外面的员工，手里有急件要交
pendingFunctors_	门口的意见箱

问题:员工把纸条塞进意见箱，经理根本不知道 —— 他在睡觉，不会定时去翻箱子

所以本关要做的事只有一件：
员工：塞纸条（push_back）+ 按门铃（write eventfd）
                              ↓
经理：门铃响了 → 醒来 → 翻箱子（swap）→ 把纸条全办了

为什么不能用“定时醒来翻一眼意见箱”？ 
因为那是轮询：醒来太频繁会浪费 CPU，醒来太慢又有延迟，而且延迟时间无法预测。门铃是“有事才响”，零延迟、零空转。

做完之后：main 里能拿到一个跑在别的线程的 EventLoop*，投任务进去能被执行，而且即使 loop 正在阻塞也能立刻执行。

### 为什么用 eventfd
方案	fd 数	要管理什么
eventfd	1 个	只有一个 8 字节计数器：write 加一变成可读，read 读出并清零
pipe	2 个	缓冲区写满要处理、要决定读几个字节、要防阻塞写
socketpair	2 个	同上，还多了协议栈开销

eventfd 最妙的地方：多次 write 会被内核合并成一个计数。
10 个线程同时按门铃，经理只需要醒来一次 —— 天然去重，这正是门铃该有的语义。

### 什么是主线程、子线程
主线程
通常指进程启动后默认执行的那个线程
主线程也是普通线程，只是它是程序入口所在的线程

子线程
由主线程或其他线程通过 pthread_create、std::thread 等创建的线程，叫子线程 / 工作线程 / IO 线程

关系
主线程和子线程不是“上下级函数调用关系”，而是并发执行关系。

T13 :
主线程                         子线程
  |                              |
  | 创建 EventLoopThread         |
  | startLoop()                  |
  | 等待子线程初始化完成   <----> 子线程构造 EventLoop
  | 拿到 EventLoop*              |
  |                              | loop.loop() 阻塞在 epoll_wait
  | runInLoop(fn) --------按铃--> 子线程醒来执行 fn


# EventLoop.h 

### 改动对照

成员 / 方法	      T12	T13	为什么要改
pendingFunctors_	裸 vector	加 std::mutex mutex_ 保护	现在会被别的线程 push_back
callingPendingFunctors_	无	bool	queueInLoop 要判断“是不是正在执行任务”
threadId_	无	std::thread::id	判断“我是不是 loop 线程”
wakeupFd_	无	int = -1	门铃本体
wakeupChannel_	无	unique_ptr<Channel>	门铃要注册进 epoll 才能响
queueInLoop	inline 一行	挪到 .cpp	要加锁 + 要按门铃
runInLoop	无	新增	本线程直接跑，跨线程才排队
wakeup / handleWakeup	无	新增 private	按铃 / 拆铃

### 成员声明顺序
一条规则顶两句：poller_ 写在最前，wakeupChannel_ 写在它后面。这样构造和析构两个方向都对。
声明顺序写反了会怎样？ wakeupChannel_ 先死、poller_ 还在——没事；
但如果 poller_ 先死、wakeupChannel_ 后死，~Channel 调用 poller_.remove() 就是访问已析构的对象 → UAF。

### callingPendingFunctors_ 是什么
一句话：标记“值班员此刻正在拆箱子”。
                        谁写	                        谁读
doPendingFunctors()	loop 线程写 true / false	—
queueInLoop()	      —	                        任意线程读

if (!isInLoopThread() || callingPendingFunctors_)
第二个条件解决的是这个场景


# EventLoop.cpp

### 构造函数 —— 装门铃
1. 点	说明
EFD_CLOEXEC	exec 后自动关，防止 fd 泄漏进子进程（T12 里 accept4 的 SOCK_CLOEXEC 是同一个理由）
EFD_NONBLOCK	read 时没数据返回 EAGAIN 而不是阻塞；write 时计数满返回 EAGAIN。loop 里其实不会触发，属防御
make_unique	和 T12 的 acceptChannel_ 同样的理由：让构造与“注册进 epoll”解耦，也便于精确控制析构时机
throw 不是 abort	构造失败让上层决定怎么处理；但注意构造抛异常不会调析构，所以此时没有 fd 泄漏（wakeupFd_ 还没被成功赋值前的失败不用管）

fd 的所有者负责关。Channel 是借用者（它只负责 epoll_ctl，不负责 close）。
所以 ~EventLoop() 必须自己 ::close(wakeupFd_)

2. eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC) 在干什么
int eventfd(unsigned int initval, int flags);
它创建的是一个由内核维护的 uint64_t 计数器，并且给你一个 fd 指向它。这个 fd 支持两个动作：

系统调用	语义
write(fd, &v, 8)	counter += v
read(fd, &v, 8)	读出 counter，然后 counter = 0

counter > 0 时，这个 fd 在 epoll 里是可读的；counter == 0 时不可读。


### 析构函数
顺序和 T9/T11 那条铁律完全一样：disableAll() → ::close() → 析构。
// 先从 epoll 摘掉（此时 poller_ 还活着）
// 再关 fd（此时 epoll 已不管它）
// wakeupChannel_ 随后被成员逆序析构；它还会 remove 一次，靠吞 ENOENT 兜底

### wakeup() —— 按门铃
必须写满 8 字节（uint64_t）。写 1 字节、4 字节 → EINVAL。eventfd 只认 8 字节的 uint64_t。
忽略返回值是对的。如果返回 EAGAIN，说明计数已经接近 2^64-1——门铃已经在响了，不用再按。

uint64_t one = 1; 是什么
uint64_t = 固定 8 字节无符号整数
one = 1 的含义是“按一次门铃”，即让计数器 +1。
为什么必须是 uint64_t：eventfd 的 read/write 只接受恰好 8 字节。否则内核直接返回 EINVAL。

为什么写 sizeof one 而不是 sizeof(uint64_t)：
将来 one 改成 uint32_t，自动跟着变

static_cast<void>(n);处理什么
n 被赋值但从没被读过 → GCC 的 -Wall 会报 -Wunused-but-set-variable。
把表达式的结果显式转换成 void，等于告诉编译器“我看到了，我故意不用”，警告就消失了。

### handleWakeup() —— 接门铃
一次 read 就清零——这是 eventfd 的语义（读出当前计数，同时置 0），所以不需要 while 循环。

### queueInLoop() —— 塞纸条 + 按铃
条件	场景	不按铃的后果
!isInLoopThread()	别的线程投的，值班员正在 epoll_wait 打盹	任务永远不执行（除非碰巧有 IO）
callingPendingFunctors_	本线程正在执行上一批任务时又投了一个	这批执行完直接回去 epoll_wait(-1)，新任务要等下个 IO 事件，可能是几小时后

锁只包住 push_back，不包 wakeup()。原因是 wakeup() 是 syscall，占锁时间没必要长

### runInLoop() —— 本线程同步，跨线程异步
调用者	      行为	      返回时任务执行了吗
loop 线程自己	直接调 fn()	✅ 执行完了
其他线程	      入队 + 按铃	❌ 可能还没开始

为什么需要这个“自动分流”？ 因为代码写起来不用关心“我在哪个线程”。
runInLoop 是安全版，你在任何地方调它都不会死锁、不会丢任务。

### doPendingFunctors() —— 拆箱子
做法	不这样会怎样

锁内 swap	
直接 for (f : pendingFunctors_) + 执行中 push_back 扩容 → 迭代器失效；
且新任务被本轮当场执行 → 无限循环（T12 已讲过）

锁外执行 functor	
f() 里调 runInLoop/queueInLoop → 再次 lock_guard(mutex_) → std::mutex 不可重入 → 死锁
callingPendingFunctors_ 是成员不是局部	queueInLoop 要读它。局部变量别的线程看不见

callingPendingFunctors_ 的完整闭环：
doPendingFunctors 置 true → 期间任何线程（含本线程）queueInLoop 都必然按铃 → doPendingFunctors 置 false。

### runOnce()
只有一处隐含变化：active_ 里多了一个 wakeupChannel_。所以一次唤醒的完整顺序是：
epoll_wait 返回（eventfd 可读）
  → for 循环里某个 ch 是 wakeupChannel_ → handleEvent → 撕 read 纸条 → handleWakeup()（读清计数）
  → 其他 IO 事件
  → doPendingFunctors()  → 执行所有排队的任务

### quit() —— 不补就会卡死
EventLoopThread::~EventLoopThread() 
exiting_ = true;                 // ← 谁读这个变量？
exiting_ 没有接入任何退出机制。loop.loop() 是 while (true) runOnce();，epoll_wait(-1) 无限阻塞，join() 会永久卡住

1. EventLoop 加 quit()
2. EventLoopThread 的析构改成调 quit()

### 关键约定：loop() 必须在构造它的线程里调用。
这就是 EventLoopThread 里 EventLoop loop; 必须写在 threadFunc() 内部的原因——写在线程里构造，threadId_ 才是子线程的 id

# 排队插队按铃过程以及代码机制

### 三个角色的状态图
演员	代码里的东西	一句话职责
值班员	loop 线程（执行 runOnce 的那个）	平时趴在 epoll_wait(-1) 打盹，有事件或被叫醒就干活
纸条箱	pendingFunctors_（vector）	存“待办任务”，谁都能往里塞（受 mutex_ 保护）
门铃	wakeupFd_（eventfd）	按一下 → epoll_wait 立刻返回；读一次 → 恢复安静
拆箱子	doPendingFunctors()	把箱子整个倒出来，逐个执行

### 五个场景：排队、插队、按铃
1. 场景 A：值班员自己写纸条 —— 插队，不排队
void EventLoop::runInLoop(Functor fn) {
    if (isInLoopThread()) {
        fn();                  // ← 当场执行，纸条根本不进箱子
        return;
    }
    ...
}

2. 场景 B：快递员来送纸条 —— 排队 + 按铃
void EventLoop::queueInLoop(Functor fn) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.push_back(std::move(fn));    // ① 放纸条（锁内）
    }
    if (!isInLoopThread()) {
        wakeup();                                      // ② 按铃
    }
}

3. 场景 C：值班员正在拆箱子时，又来纸条 —— 也必须按铃
if (!isInLoopThread() || callingPendingFunctors_) {
    wakeup();
}

callingPendingFunctors_ 存在的全部理由：
值班员在拆箱子时，会暂时“听不见门铃”（因为他不在 epoll_wait 里）；
但门铃的“响声”会被内核记在计数器上，等他下一轮回到 epoll_wait 时立刻返回。
所以要保证——拆箱子期间塞的纸条，也要留下一个“响声”。

4. 场景 D：连投 10 个任务 —— eventfd 自动合并
主线程连续 10 次 queueInLoop:
  write(fd, 1, 8) × 10  →  counter = 10
                          fd 一直处于“可读”状态（不会重复排队）

值班员:
  epoll_wait 返回（一次）
  handleWakeup() → read(fd, &v, 8) → v = 10, counter = 0
  doPendingFunctors() → swap 出 10 个任务，一次执行完

5. 场景 E：quit() —— 退出也要按铃
void EventLoop::quit() {
    quit_ = true;
    if (!isInLoopThread()) {
        wakeup();               // ← 不按，epoll_wait(-1) 就永远不返回
    }
}

### 一次完整的时间轴
时刻	线程	动作	数据/状态变化
t1	主	sub->runInLoop(fn)	不是 loop 线程 → 进 queueInLoop
t2	主	lock(mutex_) → push_back(fn)	箱子: [fn]
t3	主	unlock → write(eventfd, 1, 8)	计数器: 0 → 1，fd 可读
t4	子	epoll_wait(-1) 返回	active_ 含 wakeupChannel_
t5	子	for (ch : active_) ch->handleEvent()	撕 wakeupChannel_ 的 read 纸条
t6	子	handleWakeup() → read(fd, &v, 8)	计数器: 1 → 0，fd 变不可读
t7	子	doPendingFunctors()	tmp.swap()，箱子: [fn] → []
t8	子	callingPendingFunctors_ = true → fn()	任务在子线程执行
t9	子	callingPendingFunctors_ = false	完成
t10	子	回到 loop()，检查 quit_ → 再进 runOnce	继续下一轮


# EventLoopThread

### 它解决什么问题
EventLoop 在 T12 之前都是 main 栈上的对象，跑在主线程。
T13 要支持把 loop 塞到独立线程，对外给一个 EventLoop*，让主线程能往上投任务。

### 变量、函数
变量	类型	一句话职责	所有权
loop_	EventLoop*	产出物：子线程里那个 EventLoop 的地址	借用（不是它创建的，join 后就失效）
thread_	std::thread	工人：子线程句柄	拥有（必须 join 或 detach）
mutex_	std::mutex	门锁：保护 loop_ 的读写	拥有（值成员）
cond_	std::condition_variable	对讲机：告诉主线程“工牌挂好了”	拥有（值成员）

函数	谁调用	在哪跑	职责
EventLoopThread()	主线程	主线程	什么都不做（= default）。真正的活在 startLoop 里
startLoop()	主线程	主线程	招人 + 等他就位 + 交货（返回 EventLoop*）
threadFunc()	std::thread	子线程	子线程的全部人生：建 loop → 挂工牌 → 通知 → 营业到打烊
~EventLoopThread()	主线程	主线程	打烊：通知下班（quit）+ 等人走干净（join）

### 双线程时间轴（这张图是核心）
主线程                                    子线程
─────────────────────────────────────────────────────────────
t1  thread_ = std::thread(...)  ──────→  t1' 线程启动，准备执行 threadFunc
                                                ↓
t2  mutex_.lock()                       t2' EventLoop loop;   ← 布置店（threadId_ 登记）
    cond_.wait(loop_ != nullptr)              ↓
    └── 释放锁，睡下（不烧 CPU）         t3' mutex_.lock()
                                                ↓
                                        t4' loop_ = &loop;    ← 挂工牌
                                                ↓
                                        t5' mutex_.unlock()
                                                ↓
                                        t6' cond_.notify_one()  ──┐
                                                ↓                 │
                                        t7' loop.loop();         │
                                            └ 进入 epoll_wait(-1) │
                                                                  │
t3  ←───────────────── 被唤醒 ────────────────────────────────────┘
    mutex_.lock()（重新拿到锁）
    loop = loop_;        ← 这时保证非空
    mutex_.unlock()
t4  return loop;         ← 交货

1. t2 的 wait 必须持锁	否则会丢通知：如果 notify 发生在 wait 之前，主线程会永远睡下去（典型的 lost wakeup）

2. t4' 的赋值 必须持锁	主线程在 t3 读 loop_，两边必须串行，否则数据竞争

3. wait 用 std::unique_lock	lock_guard 不能中途解锁，wait 内部需要“释放锁 → 睡 → 重新加锁”


### ~EventLoopThread()
先 quit 再 join
loop->quit();        // 置 quit_ = true 并 wakeup() → epoll_wait 返回
thread_.join();      // 等子线程执行完 threadFunc

顺序反了会怎样？
写法	后果
先 join() 再 quit()	join 卡死：loop 还在 epoll_wait(-1)，永不返回
只 detach() 不 join()	主线程退出时子线程可能还在跑，loop_ 指向已析构的栈对象 → 程序退出时崩
只 quit() 不 join()	std::thread 析构时若仍 joinable → std::terminate 直接终止进程

### threadFunc()：为什么 EventLoop loop; 必须是局部变量
写法	结果
局部变量（正确）	在子线程执行构造函数 → threadId_ = 子线程 ✓
成员变量（错误）	在主线程构造 → threadId_ = 主线程 → 子线程跑 loop() 时 isInLoopThread() 永远 false ✗

threadId_ 认的是“构造它的线程”，不是“运行它的线程”。

### startLoop()：为什么需要条件变量
主线程：thread_ = std::thread(...)   ← 线程刚创建，可能还没被调度
主线程：return loop_;                 ← 如果直接取 loop_，还是 nullptr！
                   ↑
子线程：（可能几百微秒后才跑到）loop_ = &loop;

cond_.wait(lock, [this]{ return loop_ != nullptr; }) 保证：主线程返回前，子线程一定已经写完 loop_。

### 四个必考点
#	问题	答案要点
1	EventLoop 为什么必须在线程内构造？	threadId_ 取的是构造线程的 id；它必须等于运行 loop() 的线程
2	startLoop() 为什么需要条件变量？	主线程可能在子线程跑到 loop_ = &loop 之前就取指针 → 空指针；cond 保证“赋值完成”后才返回
3	析构为什么必须 quit() 再 join()？	epoll_wait(-1) 无限阻塞，只能靠 wakeup() 打断；不 join 则 std::thread 析构触发 std::terminate
4	loop_ 的生命周期边界在哪？	指向子线程栈对象，loop.loop() 返回后即析构 → join() 之后不可再访问

# mian
没有改动

# eventloop_thread_test.cpp
实验	代码做的事	验证的承诺	预期
1. 跨线程投递	主线程 runInLoop 一个打印线程 id 的 lambda	任务真的在子线程执行	打印的子线程 id 与主线程 id 不同
2. 唤醒延迟	先 sleep(1s) 让子线程睡死在 epoll_wait，再投任务并计时	eventfd 能打断阻塞	延迟 0–2 ms，绝不能是 1000ms
3. 同步语义	主线程调 runInLoop，前后各打一行	跨线程时不阻塞（异步）	before → after 先打印，任务稍后才在子线程跑



# 从头顺一遍：13 关讲的是一个什么故事
从“只能接待一个人的小店”，做成“一个店长 + 多个服务员 + 分店对讲机”的正经餐厅。

关	餐厅里发生了什么	技术
T1–T3	一家只能接待一个人的店：客人来了坐下，你全程陪着他，别人排队	socket/bind/listen/accept + 阻塞 read
T4	你学会同时照看多桌（但每次都全店问一遍“谁要什么”）	select / poll
T5	每桌装呼叫铃，只响一次；你必须把客人要说的话一次听完	epoll + ET 模式
T6–T7	给每桌配记事本：客人话说一半、两句连一起，你都能对上	Buffer + 长度字段切包
T8	把“看铃”这件事封成一个门卫	EpollPoller
T9	每桌一个呼叫器，门卫直接告诉你“3 号桌”	Channel + data.ptr
T10	设一个前台，统管所有铃和记事本	EventLoop
T11	每桌配专属服务员，各自负责撤台打扫	TcpConnection
T12	请一个店长：开新桌、派服务员、记台账	TcpServer + queueInLoop
T13	开了分店，两边用对讲机沟通（不用跑过去喊）	EventLoopThread + eventfd


举例：一个客人的完整旅程（对应每一关）
客人推门 → T12 店长 handleAccept 接进门、分配 3 号桌
         → T11 服务员（TcpConnection）就位，开始盯着这桌
         → T6  记事本（Buffer）记下客人说的话
         → T7  判断“话说完了吗”：没说完等（半包），说多了拆开（粘包）
         → T9  3 号桌的呼叫铃（Channel）响了 → 门卫（T8）通知前台（T10）
         → 服务员端上回显
         → 客人起身走人（FIN）→ 服务员撤台（handleClose）
         → 店长在台账上划掉这一桌（queueInLoop → erase）
