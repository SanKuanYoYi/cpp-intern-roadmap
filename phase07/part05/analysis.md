<!-->
文档解析，供于查看
<-->



T10 里散落的东西	负责什么
struct Conn { Buffer in, out; Channel ch; }	状态容器
on_read(Conn*)	怎么读
try_flush(Conn*)	怎么发
close_conn(int)	什么时候该关（其实只知道“怎么销毁”）

T11 要做的就是把前三件 + “该不该关”的判断收进一个类，main 里只剩业务。
T10                                    T11
─────────────────────────────────────────────────────────────
struct Conn + 3 个自由函数      →      class TcpConnection
on_read(Conn*)                  →      handleRead()
try_flush(Conn*)                →      tryFlush() / send()
if (n == 0) closeLater(fd)      →      handleClose()（内含判断）
close_conn(int)                 →      只剩 closeHandler 里 3 行
业务：out.append(...)           →      业务：c->send(...)


# TcpConnection.h

### 两个回调类型（重点）
MessageCallback 为什么传 Buffer&？
因为切包是业务的事，而切包必须能 retrieve（吃字节）。
如果只传 TcpConnection*，业务就得 c->in() 拿 Buffer，那不如直接给引用。

CloseCallback 为什么传 TcpConnection* 而不是 int fd？
因为上层要做的事是 conns.erase(fd)，需要 key。
传指针就能 c->fd() 拿 key，同时保留“这是个连接对象”的语义。

### 同名陷阱：两个 closeCallback
层	名字	触发人	含义
Channel	closeCallback_	内核（EPOLLHUP / EPOLLRDHUP）	“对端可能走了”
TcpConnection	closeCallback_	自己调（handleClose() 里）	“我这条连接确定要关了”

两个词一模一样，但一个是“别人告诉我”，一个是“我告诉别人”。

### ~TcpConnection() 不是 = default
析构函数里有活儿要干
TcpConnection::~TcpConnection() {
    channel_.remove();     // 不管从哪条路死，都先让 epoll 忘掉我
}


### forceClose() —— 错误路径的入口
                    谁调	                            场景
handleClose()	内部（handleRead 读到 0、handleError）	自己发现的
forceClose()	内部或外部	主动求死（比如业务发现协议违规）

### 私有：4 个 handle + 1 个 flush
T11 私有函数	T10 的 main 里
handleRead()	on_read(Conn* c)
handleWrite()	on_write(Conn* c)
tryFlush()	    try_flush(Conn* c) 的 while 部分
handleClose()	if (n == 0) loop.closeLater(fd) 那两行的升级版
handleError()	loop.closeLater(fd)（errorCb 里）


### 成员变量
成员	形式	                理由
loop_	EventLoop*	        它不属于我，属于 main/IO 线程。我只借来用
channel_	Channel（值）	它就是我的一部分，出生一起生、死一起死
in_ / out_	Buffer（值）	同上
closed_	bool	            状态标记


# TcpConnection.cpp

### send() 要判 closed_
业务可能在 MessageCallback 里对一批帧逐个 send，中途某一次 write 撞了 EPIPE → forceClose() →
closed_ = true。如果后面的帧还继续 send，就会往已关闭的 fd 上写 

### handleRead() 的循环里也要看 closed_
以前 on_read 是自由函数，关闭只能靠 closeLater（延后），安全；
现在 send 是同步的，forceClose 可能在 messageCallback_ 里当场把状态改了。
所以每调一次业务回调，都要重新检查一遍。

### closed_ 到底防什么
closed_ 防的是：同一轮里，有多条路同时发现“这个连接死了”，而 closeCallback_ 只能通知一次。
                    ┌──────────────────────────┐
                    │  同一个 Channel 的一个   │
                    │  handleEvent()           │
                    └────────────┬─────────────┘
                                 │
        ┌────────────────────────┼────────────────────────┐
        ▼                        ▼                        ▼
  ① revents 含           ② revents 含            ③ revents 含
     EPOLLIN                EPOLLRDHUP             EPOLLERR
        │                        │                        │
        ▼                        ▼                        ▼
   handleRead()            closeCallback_            errorCallback_
        │                        │                        │
   readFd()==0 或 <0             │                        │
        └────────────┬───────────┴────────────────────────┘
                     ▼
              handleClose()  ← 可能被调 2~3 次！
                     │
        ┌────────────▼─────────────┐
        │ if (closed_) return;  ★  │  ← 只有第一次放行
        │ closed_ = true;           │
        │ closeCallback_(this);     │
        └────────────┬─────────────┘
                     ▼
           loop.closeLater(fd)    ← 上层只登记一次

让“我要死”这件事只发生一次，避免重复 ::close 误伤复用的 fd 号。

### 五个 handle 的全貌
                    内核报的事件
        ┌──────────┬──────────┬─────────┐
     EPOLLIN   EPOLLOUT     HUP        ERR
        │          │          │          │
        ▼          ▼          ▼          ▼
  handleRead  handleWrite  handleClose  handleError
        │          │          │            │
        │          └──► tryFlush()        │
        │                 ▲  │             │
        │                 │  └── 真错 ─────┐│
        │            send()                ▼▼
        │                 ▲            forceClose()
        │                 │            (=handleClose)
        └── read==0 ──────┴──► handleClose()
                                   │
                                   ▼
                        closeCallback_(this)
                                   │
                                   ▼
                          loop.closeLater(fd)
                                   │
                          ┌────────┴────────┐
                          │  本轮结束后      │
                          │  closeHandler    │
                          │  ::close + erase │
                          └─────────────────┘

### 构造函数：挂回调，但不订阅
### 析构：让 epoll 忘掉我
为什么不能 = default：
销毁路径	                            有 remove()	                    没 remove()
closeHandler → ::close(fd) → erase	    多一次 epoll_ctl(DEL)（容错掉）	靠内核自动摘除，勉强能跑
conns.clear() / 程序退出	        ✅ epoll 干净	                ❌ data.ptr 悬空，下次 epoll_wait 崩
有人 dup() 过 fd	                    ✅ 显式摘除	        ❌ 内核不会摘，必崩

正常路径下，closeHandler 先 ::close(fd)，再 erase → ~TcpConnection → channel_.remove()。
此时 fd 已经关了，epoll_ctl(DEL, fd) 返回 EBADF。
要去 EpollPoller::remove() 加if (ret < 0 && errno != ENOENT && errno != EBADF) throw