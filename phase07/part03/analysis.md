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
①：struct epoll_event evs[64] 变成函数内局部变量
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
为什么 std::move(cb)？
cb 是按值传入（拷贝了一份 std::function），std::move 把它转移到成员，避免再拷贝一次

为什么四个回调分开存，不搞一个 std::map<uint32_t, EventCallback>？
4 个成员是固定 4 个 std::function（每个 32 字节左右），无动态分配
map 每条要一次红黑树节点分配 + 一次查找
事件类型就 4 种（读/写/关/错），固定成员最快最清晰

### enable/disable 系列：改 events_ 后调 update()
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


