<!-->
文档解析，供于查看
<-->


# EpollPoller.h
三个函数全变
T8	T9	变化本质
update(int fd, uint32_t events)	update(Channel* ch)	两个裸参数 → 一个对象，内部读 ch->fd() / ch->events()
remove(int fd)	remove(Channel* ch)	同上，内部读 ch->fd()
int wait(vector<epoll_event>&, int)	void poll(int, vector<Channel*>&)	返回值变 void，参数从"epoll_event 数组"变"Channel 列表"*
epoll_wait 的底层行为完全没变——变的是"对外怎么表达"。

class Channel; 是必须的：Channel.h 里 #include 了 EpollPoller 的声明（Channel(EpollPoller* poller, ...) 用前向声明），两边只能前向声明对方，不能互相 include。
EpollPoller.cpp 里才 #include "Channel.h" 拿完整定义。


# EpollPoller.cpp

### update(Channel*)：三处变化
①：ev.events = ch->events()
T8 是调用方传 events 进来，T9 是从 Channel 读。
T8：调用方每次调 update 都要手算 events = EPOLLIN | EPOLLET | (want_write ? EPOLLOUT : 0)
T9：调用方调 ch->enableWriting()，Channel 内部改 events_ 再调 update

②：ev.data.ptr = ch —— T9 的核心
data.ptr 让内核替你记住"这个 fd 对应哪个对象"，就绪时直接还给你。 哈希表在每次事件分发时省掉了。
data.ptr 存对象地址，内核在事件就绪时把指针原样返回，省掉 map.find(fd) 的哈希查找。
代价是对象生命周期必须和 epoll 注册严格对齐——remove 必须在对象析构之前。

③：int fd = ch->fd() 单独取出来
要先存到局部变量，不直接写 ch->fd() 三次

④：if (ch->events() == 0) { remove(ch); return; } —— 必须加
没有这个判断 → MOD 返回 EINVAL → errno != ENOENT → 直接 throw → 整个进程挂
有判断 → events_ == 0 意味着"不想关注任何事件" = 等同于从 epoll 摘掉 → 调 remove(ch) → 干净退出

### poll 代替 wait：结构性大改
1. 干一件事：等事件，把"谁就绪了"塞进 active。


2. ①：struct epoll_event evs[64] 变成函数内局部变量
T8 的 wait 返回 epoll_event 数组，调用方要自己处理 data.fd。
T9 的 poll 只在内部用 epoll_event，对外只暴露 Channel* 列表
64 是 muduo 的默认值

②：EINTR 直接 return（void 函数）
调用方（main）看到 active 为空 = 本轮没事件 = 回 while 顶部再 poll。

③：static_cast<Channel*>(evs[i].data.ptr) —— void* 转回对象指针

④：ch->setRevents(evs[i].events) 必须在 push_back 之前
如果先 push_back 再 setRevents：
    active 里的 ch 的 revents_ 还是上一轮的值（可能是 0 或旧事件）
    → handleEvent 读到旧 revents_ → 调错回调 / 什么都不调
    → 数据被跳过，客户端超时

如果先 setRevents 再 push_back：
    active 里的 ch 的 revents_ 已经是本轮就绪的事件
    → handleEvent 正确分发

坑 1：poll 里 evs[64] 固定，active 上限也是 64
坑 2：update(ch) 里 ch->events() == 0 必须先判断，且 return
坑 3：remove(ch) 之后不能再 update(ch) —— 对象已从 epoll 摘了



# Channel.h
V8 在解决什么（一句话）
把"fd 关注什么事件、就绪后调哪个函数"从 main 里的 if (fd == listen_fd) ... else if (evs & EPOLLIN) ... 散落逻辑，收进一个 Channel 对象。

main 的事件循环退化成：for (Channel* ch : active) ch->handleEvent();——它不再认识任何 fd。

Channel 的核心思路：一个 fd 一个对象，绑着"关注的事件"和"就绪时的回调"。 
事件循环只负责"谁就绪了就叫谁"，不关心它是 listen、连接、定时器还是 eventfd。

### 前向声明，避免循环 include
class EpollPoller;
为什么是前向声明，不是 #include "EpollPoller.h"？
因为 EpollPoller.h 也要用到 Channel。如果两边互相 #include，头文件循环。
前向声明只告诉编译器"有个类叫 EpollPoller"，指针/引用不需要完整定义，
等到 Channel.cpp 里真正调 poller_->update(this) 时再 #include "EpollPoller.h"。

### using EventCallback = std::function<void()>
为什么用 std::function 而不是函数指针 void(*)()？

函数指针	std::function
捕获外部变量（lambda）	❌ 不能	✅ 能
存成员函数绑定（bind）	❌ 麻烦	✅ 直接
开销	一次间接跳转	一次间接跳转 + 可能一次堆分配（小 lambda 有 SBO 优化）
T9 能用吗	❌ [&, raw] { on_read(raw); } 捕获了 this 和 raw，函数指针装不下	✅

std::function<void()> 就是一个"万能函数容器"：
std::function<void()> f;
f = on_read;            // ✅ 装普通函数
f = cb2;                // ✅ 装捕获 lambda
f = cb3;                // ✅ 装 bind 结果
f();                    // ✅ 调用，不管装的是什么


### 构造 + 禁拷贝
构造参数 EpollPoller* poller：Channel 不拥有 Poller，只持有指针，用它调 update/remove。
为什么禁拷贝？
Channel 是"一个 fd 的唯一代表"，不可复制。 和 EpollPoller 同理。

### events_ vs revents_ 是最容易混的一对
events_（我关注什么）	revents_（内核说就绪了什么）
谁写	enableXxx/disableXxx	setRevents（Poller 写）
谁读	EpollPoller::update	Channel::handleEvent
方向	你 → 内核（注册时告诉它）	内核 → 你（就绪后告诉你）
例子	EPOLLIN | EPOLLET	EPOLLIN（这次只有读就绪）
handleEvent 只读 revents_，不读 events_——因为"该不该处理"由"内核说就绪了什么"决定，不是"你想关注什么"。

### 四个回调 setter
1. Channel 里存了四个"空盒子"，每个对应一种事件：
EventCallback readCallback_;    // 读事件来了，调这个
EventCallback writeCallback_;   // 写事件来了，调这个
EventCallback closeCallback_;   // 对端关了，调这个
EventCallback errorCallback_;   // 出错了，调这个

初始都是空盒子（std::function 默认构造为空）。setReadCallback(cb) 就是"把 cb 装进 readCallback_ 盒子"。
void setReadCallback(EventCallback cb) { readCallback_ = std::move(cb); }
//                                                 ↑ 装进盒子


2. 为什么 std::move(cb)？
cb 是按值传入（拷贝了一份 std::function），std::move 把它转移到成员，避免再拷贝一次

4. 为什么四个回调分开存，不搞一个 std::map<uint32_t, EventCallback>？
4 个成员是固定 4 个 std::function（每个 32 字节左右），无动态分配
map 每条要一次红黑树节点分配 + 一次查找
事件类型就 4 种（读/写/关/错），固定成员最快最清晰


### enable/disable 系列：改 events_ 后调 update()
enableReading() / disableWriting() 四个方法干什么？
                                    存哪里	谁写	谁读
回调（readCallback_ 等）	Channel 的成员	setReadCallback	handleEvent
关注的事件（events_）	Channel 的成员	enable/disable	EpollPoller::update
回调是"门铃响了干什么"；events_ 是"我要不要装这个门铃"。
enableReading() = "我要装读门铃" // disableWriting() = "我要拆写门铃"
EPOLLOUT 几乎永远是就绪的（发送缓冲空着 = 可写）平时：只装读门铃（events_ = EPOLLIN）
write 遇 EAGAIN：装写门铃（events_ |= EPOLLOUT）
数据发完：拆写门铃（events_ &= ~EPOLLOUT）


每个方法两步：
改 events_（|= 加，&= ~ 去，= 0 清空）
update() 通知 Poller（内部 poller_->update(this)）

disableAll() 的妙处：events_ = 0 后 update()，Poller 里判断 ch->events() == 0 就 remove(ch)

isWriting() 给谁用？ try_flush 里防重复注册：和 T4 的 want_write 一模一样

### handleEvent() + 私有 update()
handleEvent() 声明在 public：Poller 的 poll 只把 Channel* 塞进 active
update() 是 private：外部不能直接调 ch->update()——必须经过 enableReading/disableWriting 等语义化方法。


# Channel.cpp

### 构造 / 析构
构造函数只存指针和 fd，events_ = 0、revents_ = 0 由头文件的默认成员初始化完成
析构什么都不做——为什么？
因为 Channel 不拥有 fd，也不拥有 epoll 注册。 谁拥有？
fd：由 main 里 close_conn 的 ::close(fd) 关
epoll 注册：由 poller.remove(&ch) 摘

<!-->
这体现了所有权分离：
Conn 拥有 fd（通过 Channel ch 的 fd 号和外部 close 管理）
EpollPoller 拥有 epoll 实例和注册
Channel 只是"视图/接口"，不拥有资源
<-->

### handleEvent：顺序是灵魂
顺序有讲究：错误 → 挂断 → 可读 → 可写
① EPOLLERR 优先，且 return
② EPOLLHUP 次优先，也 return
EPOLLHUP = 对端完全关闭（发 FIN 且本地也关了写）。连接终止，closeCallback_() 处理善后，return。
③ 读事件：EPOLLIN | EPOLLPRI | EPOLLRDHUP 一起判断
标志	含义	为什么归到"读"
EPOLLIN	普通数据可读	标准读事件
EPOLLPRI	TCP 紧急数据（OOB）	也是"有数据要读"，虽然一般用不上
EPOLLRDHUP	对端半关闭（shutdown WR）	对端"写完了"，但你还能读缓冲里的剩余数据——应该触发读回调让业务读到 EOF，而不是直接 close
④ 写事件：EPOLLOUT 单独判断，不 return

### Channel 和 Poller 的完整交互图
              ┌─────────────────────────────┐
              │   Channel                   │
              │   fd_                       │
              │   events_（我想关注什么）    │
              │   revents_（内核说就绪什么） │
              │   read/write/close/error cb │
              └──────┬──────────────▲───────┘
                     │              │
       enableReading │              │ setRevents
       → update()    │              │ (由 poll 写)
                     ▼              │
              ┌─────────────────────┴───────┐
              │   EpollPoller               │
              │   epfd_                     │
              │                             │
              │   update(ch):               │
              │     ev.events = ch->events()│
              │     ev.data.ptr = ch        │
              │     epoll_ctl(MOD/ADD)      │
              │                             │
              │   poll(active):             │
              │     epoll_wait(...)         │
              │     for 每个就绪:            │
              │       ch = data.ptr         │
              │       ch->setRevents(...)   │
              │       active.push_back(ch)  │
              └─────────────────────────────┘
                     │
                     ▼
              main 事件循环：
              for (Channel* ch : active) ch->handleEvent();


### 一张图把五个概念串起来
┌──────────────────────────────────────────────────────┐
│ Channel（一个 fd 的房间）                             │
│                                                      │
│  events_  ← 开关面板：当前关注哪些事件（enable/disable）│
│                                                      │
│  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐         │
│  │读门铃  │ │写门铃  │ │关门铃  │ │错门铃  │         │
│  │cb_读   │ │cb_写   │ │cb_关   │ │cb_错   │         │
│  └────────┘ └────────┘ └────────┘ └────────┘         │
│      ↑                                                │
│      └── setXxxCallback 贴铃                          │
│                                                      │
│  revents_ ← 内核说"这次就绪了什么"                    │
│                                                      │
│  handleEvent() → 按 revents_ 决定按哪个铃            │
└──────────────────────────────────────────────────────┘
        ↑                    ↓
        │                    │
        │ update()           │ handleEvent()
        │                    │
┌───────┴──────┐    ┌────────┴─────────┐
│ EpollPoller  │    │ main             │
│ poll()       │───→│ for (ch: active) │
│ 返回 Channel*│    │   ch->handleEvent│
└──────────────┘    └──────────────────┘

东西	一句话
std::function<void()>	能装任意无参函数的盒子
EventCallback	给上面那个盒子起的外号，少打字
四个 setXxxCallback	把回调贴到对应的门铃下面
enable/disableXxx	拨开关：我要/不要装这个门铃；拨完通知内核
poll	前台叫号：谁就绪了，塞进 active 列表
handleEvent	铃响了：按 revents_ 决定按哪个铃，调对应回调



# main_V8.cpp

### 数据结构：Conn 内嵌 Channel，conns 用 unique_ptr
① want_write 删掉
want_write 的物理存储就是 events_ 里的 EPOLLOUT 位。T9 用 ch.isWriting() 替代

② Conn 内嵌 Channel ch
为什么是值成员，不是 Channel*？
Channel 和 Conn 生命周期一致（Conn 活多久，Channel 活多久）
值成员零额外分配，Conn 构造时 ch 一起构造

③ conns 从 unordered_map<int, Conn> 变 std::map<int, unique_ptr<Conn>>
为什么必须用 unique_ptr？
// ❌ 用值存储
std::unordered_map<int, Conn> conns;
conns[fd] = Conn(...);
// 如果 unordered_map 扩容 → 所有节点重新哈希 → Conn 对象搬家
// → data.ptr 指向的地址变了 → epoll 里的 ptr 是旧地址 → 野指针！
// ✅ 用 unique_ptr
conns[fd] = std::make_unique<Conn>(...);
// Conn 在堆上，地址永远不变，data.ptr 永远有效

### 监听 fd 也变成 Channel
listenCh.setReadCallback([&] { on_accept(); });     // listen 的读回调 = accept
raw->ch.setReadCallback([&, raw] { on_read(raw); }); // conn 的读回调 = 读数据

main 的主循环不再需要 if (fd == listen_fd)——listenCh 和 connCh 都只是 active 里的一个 Channel*，handleEvent 各调各的回调。

listenCh 不需要 closeCallback/errorCallback——listen_fd 是主 socket，出错整个服务该重启，不是"关闭一个连接"。只设 readCallback 就够。


### 三个回调函数（on_accept / on_read / on_write）
T8 里这些逻辑都嵌在主循环的 if 分支里。T9 把它们提成独立函数。

① on_accept()：循环 accept 到 EAGAIN
② on_read(Conn* c)：读 → 切包 → flush
alive 布尔变量被 return 替代——"要关连接"就 push_back 然后 return，不再需要 alive = false 标记。
③ on_write(Conn* c)：一行 try_flush(c);


### close_conn：顺序是生死线
// ① 先从 epoll 摘掉  ② 再关 fd   ③ 最后销毁对象
❌ 先 close(fd) 再 remove：
    close(fd) 后，fd 号可能立刻被系统复用给新 accept 的连接
        → 新连接也注册了同一个 fd 号
    poller.remove(&ch) → epoll_ctl DEL 用的是旧 ch 的 fd
        → 但 fd 号已被新连接占用 → 删掉的是新连接的注册！
        → 新连接从此收不到事件 → 永远卡死
    → 偶发、难复现、线上才炸

❌ 先 erase 再 remove：
    conns.erase(it) → Conn 被 unique_ptr 销毁
        → Channel 析构 → 但 epoll 里还有 data.ptr = 已释放的 Channel
    poller.remove 试图访问已释放对象 → UAF
    下轮 epoll_wait 返回野指针 → 段错误

✅ remove → close → erase：
    remove：从 epoll 摘掉，不再有事件指向这个 Channel
    close：关 fd，释放系统资源
    erase：销毁 Conn（Channel 一起销毁），此时 epoll 里已无引用

### to_close 保护对象生命周期
on_read 里为什么要 to_close.push_back(fd); return; 而不是直接 close_conn(fd)？
on_read 是怎么被调用的？
    main: for (ch : active) ch->handleEvent();
        → handleEvent 调 readCallback_ → on_read(raw)
        → on_read 正在执行时，raw 是 active 里的 Channel*

如果 on_read 里直接 close_conn(fd)：
    → conns.erase → Conn 销毁 → raw 悬空
    → on_read 返回 → handleEvent 返回 → 回到 for (ch : active)
    → 下一个 ch 可能是同一个已被销毁的 Channel？不会
    → 但 active 里还有 raw 的指针，如果后面有代码访问...危险
    → 更关键：handleEvent 里 on_read 返回后，handleEvent 还要继续往下执行
       （比如检查 EPOLLOUT 分支）→ 访问已销毁的 Channel → UAF

所以：回调里只登记，统一在 for 循环之后关闭：
    for (ch : active) ch->handleEvent();   ← 期间 Conn 都活着
    for (fd : to_close) close_conn(fd);    ← 循环结束后才销毁
    to_close.clear();


### 建连接：raw 指针的捕获
为什么用 raw 裸指针捕获？
unique_ptr 不能拷贝，lambda 捕获 c 会编译失败：
raw = c.get() 拿裸指针，所有权仍在 c（之后转移到 conns）。 lambda 捕获 raw，回调时用 raw 访问对象


### 坑
坑 1：回调里绝不 conns.erase
坑 2：raw 裸指针的有效性依赖 conns
坑 3：close_conn 的幂等性


### 指针和引用
位置	写法	为什么
EpollPoller* poller_	指针	Channel 不拥有 Poller，可能为空（理论上），且将来可能改指向（如果支持切换 Poller 实现）
update(Channel* ch)	指针	需要"这个 fd 的 Channel"这个概念——Channel 是"被管理的对象"，可能是空的（remove(nullptr) 应该被拒绝），但主要原因是 data.ptr 是 void*，转回来必须是指针**
poll(..., vector<Channel*>& active)	引用	active 必须存在（调用方保证），函数要往里 push，引用让调用方写 poll(active) 而非 poll(&active)
handleEvent()	无参	因为是成员函数，this 就是对象
process_frames(Buffer& in, Buffer& out)	引用	必须存在、要修改、大对象——三重理由
readFd(int fd, int* saveErrno)	指针	saveErrno 是可选输出——如果调用方不关心错误码，可以传 nullptr。这是 C 风格"可选输出参数"的惯例
Conn* raw（lambda 捕获）	指针	每个连接一份独立副本，按值捕获指针恰好实现这个——如果按引用捕获，会被下一次 accept 覆盖
std::unique_ptr<Conn>	智能指针	显式所有权——conns 拥有 Conn 的生命周期
for (Channel* ch : active)	指针（迭代）	active 存的就是指针


# 一次完整流程（从连接到关闭）
① 建连接（on_accept 里）
   Channel ch(&poller, conn_fd)       ← 新建前台，管 fd
   ch.setReadCallback(on_read)        ← 读电话设号码：on_read
   ch.setWriteCallback(on_write)      ← 写电话设号码：on_write
   ch.setCloseCallback(on_close)      ← 关电话设号码：on_close
   ch.setErrorCallback(on_error)      ← 错电话设号码：on_error
   ch.enableReading()                 ← 拨开关：读电话开
       → events_ |= EPOLLIN
       → poller.update(ch) → epoll_ctl ADD
   
② 客户端发数据
   内核 → epoll_wait 返回 → poller.poll 把 ch 塞进 active
   main: ch->handleEvent()
       → revents_ = EPOLLIN
       → 读电话开关开了吗？开了
       → readCallback_ 非空吗？非空
       → 调用 readCallback_() = on_read(raw)
   
③ on_read 里读 → 切包 → try_flush 回包
   
④ try_flush 遇 EAGAIN（发送缓冲满）
   if (!ch.isWriting())            ← 写电话当前关着
       ch.enableWriting()          ← 开写电话
           → events_ |= EPOLLOUT
           → poller.update(ch) → epoll_ctl MOD（现在监听读写）
   
⑤ 发送缓冲腾空，内核说可写
   ch->handleEvent()
       → revents_ = EPOLLOUT
       → 写电话开关开了吗？开了
       → writeCallback_ 非空吗？非空
       → 调用 writeCallback_() = on_write(raw)
   
⑥ on_write 里 try_flush 把剩余数据发完
   if (ch.isWriting())             ← 写电话还开着
       ch.disableWriting()         ← 关写电话
           → events_ &= ~EPOLLOUT
           → poller.update(ch) → epoll_ctl MOD（现在只监听读）
   
⑦ 客户端关了
   内核 → revents_ = EPOLLHUP
   handleEvent → 关电话开关开了吗？开了 → closeCallback_() → push_close

revents_ 只代表"内核说发生了什么"，真正调哪个回调还要看"这次这个事件你设了回调没"（&& readCallback_）


┌───────────────────────────────────────────────────┐
│ Channel（前台）                                     │
│                                                   │
│  ┌─ 开关面板（events_）──────────────────────────┐ │
│  │  读电话 [✓]   写电话 [✗]                      │ │
│  │  ← EPOLLIN 位  ← EPOLLOUT 位                 │ │
│  │  由 enableReading/disableWriting 控制         │ │
│  └───────────────────────────────────────────────┘ │
│                                                   │
│  ┌─ 四部电话（回调）──────────────────────────────┐│
│  │  ☎读  → readCallback_  = on_read              ││
│  │  ☎写  → writeCallback_ = on_write             ││
│  │  ☎关  → closeCallback_ = on_close             ││
│  │  ☎错  → errorCallback_ = on_error             ││
│  │  由 setXxxCallback 设号码                     ││
│  └───────────────────────────────────────────────┘ │
│                                                   │
│  ┌─ 内核通知的实际情况（revents_）───────────────┐│
│  │  "这次 EPOLLIN 就绪了"                         ││
│  └───────────────────────────────────────────────┘│
│                                                   │
│  handleEvent() = 看 revents_ → 对就绪事件           │
│                  检查开关 + 号码 → 响对应电话        │
└───────────────────────────────────────────────────┘


概念	一句话	生活对照
setXxxCallback(cb)	给某部电话设转接号码	把你的手机号存到"读电话"上
enableXxx()	打开某部电话的开关	拨开开关面板上的"读电话"
disableXxx()	关掉某部电话的开关	拨关"读电话"（比如打烊）
handleEvent()	前台按 revents_ 响电话	客户来了：看哪个电话该响 → 响它 → 转给你

enable/disable 决定"要不要被通知"（开关），setXxxCallback 决定"被通知后干什么"（号码）。两件事完全独立，都要做才能工作。


# 杂
Buffer 里没有 fd，只有 buf_ / readerIndex_ / writerIndex_。它跟 fd 一点关系都没有

对端关闭连接触发的 EPOLLIN——read 返回 0，代码据此关闭。记住：对端关闭也会表现为“可读”

fd 复用：真实存在，但那是 close 之后内核的行为，跟你 erase 无关
真正的问题：epoll 里存的是 Channel*（data.ptr），Conn 一 erase，那个地址就失效了。
下一个事件到来，handleEvent() 会访问一块已释放的内存——段错误或静默脏数据。这跟 fd 号无关，是指针悬空。



