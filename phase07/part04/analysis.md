<!-->
文档解析，供于查看
<-->



T10 要做的只有一件事：把这坨东西整个搬到类里，名字叫 EventLoop。
main() 里一句 while 都没有，只剩 loop.loop();
"什么时候该干活"归 EventLoop，"干什么活"归回调
main() 里看不到任何 epoll 相关的东西（epoll_、EPOLLIN、poller）

一句话类比：T9 你是"老板兼厨师"，一边盯着门口看谁来了，一边亲自炒菜。
T10 是雇一个值班经理（EventLoop），他只负责"叫号"，炒菜还是你的厨房（回调）干。
老板（main）从此只负责"开店、挂招牌、然后坐着"

# EventLoop 和 EpollPoller 的区别
Poller 是“一次查询”，EventLoop 是“一段循环”。

维度	EpollPoller	EventLoop
核心职责	包住 3 个 syscall（epoll_create/ctl/wait）	包住“一轮工作的调度顺序”
抽象层次	系统调用层	事件循环层
自有状态	只有 epoll fd（真正的状态表在内核里）	poller_ + active_ + pendingClose_ + closeHandler_
有无“时间/轮次”概念	无（-1 是调用者给的）	有（一次 runOnce = 一轮心跳）
会不会调回调	绝不（只返回 Channel* 列表）	会（handleEvent + closeHandler_）
知不知道 Channel	知道指针，不知道语义	知道 handleEvent() 这个统一接口
分配/复用缓冲区	无（evs[64] 是栈上临时）	有（active_ 常驻复用）
生命周期	是 EventLoop 的零件	是线程的骨架，一个线程一个

EpollPoller = 传达室的门卫。他只干一件事：有人来找你，他记名字；你要他说谁来过，他照着单子念。
他不管你为什么找他，也不管你听完之后干什么。
EventLoop = 值班经理。他决定“多久去问一次门卫”（poll 的时机）、“念到名字先处理谁”
（遍历 active_）、“谁的事散会后办”（pendingClose_）、“一轮什么时候算完”（clear 之后）


# 全局图
                    ┌─────────────── main.cpp（店长）───────────────┐
                    │  开店 listen、造 Channel、挂回调、loop.loop()   │
                    └───────────────┬──────────────────────────────┘
                                    │ ① 搭架子（一次性）
                    ┌───────────────▼──────────────────────────────┐
                    │            EventLoop（值班经理）              │
                    │   一轮一轮地：问 → 派活 → 清场                 │
                    │   手里攥着：poller_ / active_ / pendingClose_ │
                    └───────┬──────────────────────────▲───────────┘
               ② poll(问)    │                          │ ③ handleEvent(派活)
                    ┌───────▼──────────┐      ┌────────┴──────────┐
                    │ EpollPoller(门卫) │◄─────┤  Channel（档案卡） │
                    │  只跟内核打交道    │ ④update/remove        │
                    └───────┬──────────┘      └────────┬──────────┘
                            │ 内核事件                 │ ⑤ 回调跳回业务
                            ▼                          ▼
                         Linux 内核            Conn{ Buffer in; Buffer out; }



文件	一句话角色	它知道	它不知道	类比
Buffer	存字节的容器，三段式（已读 / 可读 / 可写）	字节、下标、怎么扩容	字节是啥协议、属于谁	备菜盘：一个装生料（in），一个装熟菜（out）
EpollPoller	把 3 个 epoll 系统调用包起来	fd、事件掩码、Channel*	什么是“读”、什么是“业务”	前台门卫：只负责“谁来了”和“记下谁要盯什么”
Channel	一个 fd 的“监控档案”	我盯哪些事件、内核报了什么、发生了叫谁、我属于哪个 loop	怎么读、怎么发、缓冲区在哪	呼叫器 + 名片：一张卡上写着“这桌按铃了叫哪个服务员”
EventLoop	节奏控制器：一轮一轮推进	顺序、时机、本轮谁该死	::close 怎么做、连接表在哪	值班经理：什么时候问、派谁、谁延后办
main.cpp	店长：搭架子，然后坐下	业务全部：accept、切帧、flush、关连接	epoll 的任何细节（T10 后）	店长：开店、装修、挂牌，然后坐着喝茶


### 上行 / 下行：两条路
1. 下行（注册，“我要被通知”）
你的代码 enableReading()
  → Channel 改自己的 events_
  → Channel::update()  →  loop_->updateChannel(this)
  → poller_->update(this)  →  epoll_ctl(MOD/ADD)

2. 上行（通知，“有人来了”）
epoll_wait 返回
  → EpollPoller 把 ev.data.ptr 塞进 active_
  → EventLoop 遍历 active_，调 ch->handleEvent()
  → Channel 看自己的 revents_，决定调哪个回调
  → readCallback_() → 你的 on_read()

Channel 是上行下行的“交汇点”，所以它同时持有 fd_、events_、revents_、四个回调、loop_。
它是整个系统里唯一“认识两端”的类 —— 一头是内核事件，一头是你的业务。


### 一次读事件，把五块串一遍
① 客户端发 "hello"
② 内核标记 fd=5 可读
③ EpollPoller::poll → epoll_wait 返回，active_ = [ch5]
④ EventLoop::runOnce → ch5->handleEvent()
⑤ Channel: revents_ 含 EPOLLIN → 调 readCallback_()
⑥ main 的 on_read:
      conn->in.readFd(fd)              ← Buffer 干活（readv 收字节）
      process_frames(conn)             ← 切帧（业务）
      try_flush(conn)                  ← 从 conn->out 往外写
         └ 遇到 EAGAIN → ch.enableWriting()
              → Channel::update() → loop_->updateChannel → poller_->update → epoll_ctl(MOD)
⑦ 如果这轮结束了要对端断：on_read 里 loop.closeLater(5)    ← 只登记
⑧ runOnce 尾部（handleEvent 全跑完了）→ closeHandler_(5)
      → close_conn: removeChannel → ::close → conns.erase



# EventLoop.h

### using CloseHandler = std::function<void(int fd)>;
这里只传一个 int fd，不传 Conn*。这是故意的：EventLoop 属于网络层，Conn 属于你的业务层，网络层绝不能 #include 上层的东西。
所以底层只喊"我要关 5 号 fd"，至于 5 号 fd 背后是哪个 Conn 对象 —— 交给上层注入的回调自己去找。

### void closeLater(int fd) { pendingClose_.push_back(fd); }
这就是你 T9 里 to_close.push_back(fd) 的换个壳的同一句话 —— 但壳换了之后语义就清楚了：
to_close 是"我的私有便签"，closeLater 是"交给值班经理的离职申请"。

### void runOnce();  void loop();      
为什么拆成两个？
可调试：    可扩展：poll(timeoutMs)     可读：loop() 就一行 for (;;) runOnce();

### private
成员	T9 时的身份	现在
poller_	main 里的局部 EpollPoller poller	收编成成员，值和对象绑在一起了
active_	main 里的局部 vector<Channel*> active	收编成成员，reserve(64) 一次，永远复用
pendingClose_	main 里的 vector<int> to_close	收编成成员
closeHandler_	不存在（直接调 close_conn(fd)）	新增的一层间接，业务动作从外部注入


# EventLoop.cpp

### EventLoop::EventLoop() { active_.reserve(64); }
active_ 一次性备好 64 个槽位，之后 push 永不触发扩容。 
T9 用 vector<Channel*>& active 出参想达到的"零分配"，现在从"调用方维护"变成"类自己维护"

### close_conn(fd) → closeHandler_(fd)
EventLoop 从"知道怎么关连接的人"变成"只会喊人关连接的人"。

if (closeHandler_) 这层判空：
没设 handler 时 pendingClose_ 会被静默丢弃（不报错、不崩、连接就是不关）
所以 setCloseHandler 是必须调用的初始化步骤





# main_V9.cpp

### 删除清单
#	删掉什么	原因
1	#include "EpollPoller.h"	main 不再认识 poller（验收要求）
2	EpollPoller poller;	收编进 EventLoop 私有成员
3	std::vector<Channel*> active;	变成 EventLoop::active_
4	std::vector<int> to_close;	变成 EventLoop::pendingClose_
5	整段 while (true) { ... }	变成 loop.loop();
6	active.clear();	runOnce 里已经干了
7	to_close.clear();	runOnce 里已经干了



### EventLoop loop; // 先声明 → 后析构 Channel listenCh(&loop, listen_fd);   // 后声明 → 先析构  ✅ 安全
Channel 里存着 EventLoop* loop_。如果 EventLoop 先死，Channel 就成了悬空指针。

