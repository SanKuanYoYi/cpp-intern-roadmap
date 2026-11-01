<!-->
此文件粘贴各 demo 的解析、说明等文档，以备需要时阅读
<-->

# http_parser.h

1. struct HttpRequest
为什么用 struct 而不是 class
HttpRequest 是纯数据聚合（POD 风格，虽然 std::string 让它非平凡），没有任何不变量需要维护、没有需要封装的私有状态。
C++ 惯例：纯数据用 struct，全部 public；有行为、有不变量用 class。
这里所有字段都应该是 public 的——调用方要直接读 req.method、req.body，加 getter 只会徒增噪声

method	请求行第一段	GET、POST、DELETE 等，原样保存大小写。HTTP 方法大小写敏感
path	请求行第二段	不包含 query string
version	请求行第三段	HTTP/1.1 或 HTTP/1.0，保存原字符串
headers	头部逐行	键是 header 名，值是 header 值（去掉了值前面的空格）
body	按 Content-Length 读的字节	原始字节，不做任何解码

headers 用 unordered_map
HTTP header 名大小写不敏感。unordered_map 是大小写敏感的
正确做法是在插入时把键统一转小写（或转大写），查找时也转。unordered_map 后一个覆盖前一个，丢失信息
哈希开销。线性查找更快，但 unordered_map 语义更清晰。

2. HttpParser：增量解析的状态机
std::string buf_;          // 累积尚未消费的字节
HttpRequest req_;          // 当前正在解析的请求
bool headers_done_ = false;// 头部是否已经解析完
size_t content_length_ = 0;// 从头部解析出的 body 长度（默认 0）

buf_ 是解析器的核心状态。feed 每次被调用时把新到的字节 append 进去，然后尝试从 buf_ 的头部切出完整请求，切出后 erase 掉已消费部分

为什么需要 buf_ 而不是每回调一次就解析一次：TCP 是字节流，一次 recv 可能拿到半个请求（头部没到 \r\n\r\n），也可能拿到一个半请求（粘包）

为什么 headers_done_ 是显式状态：第一趟 feed 可能只收齐头部（找得到 \r\n\r\n），但 body 还不够。
下一趟 feed 直接跳到"检查 body 够不够"，不能重复解析头部

content_length_ 默认 0 的含义：没有 Content-Length 头的请求（如 GET）body 长度为 0，buf_.size() < 0 永远为假，直接走"body 完成"分支。这让 GET 和 POST 走同一条路径，不需要特判。

3. 为什么是 feed(data, len) 而不是 parse(const std::string&)
增量接口：调用方（epoll 回调）每次 recv 到多少就 feed 多少，解析器自己管理"还差多少"。调用方不需要知道 HTTP 协议细节
一次性接口：调用方必须先判断"这个 buffer 里有没有一个完整请求"

4. 返回 bool 的语义
bool feed(const char* data, size_t len);
true：从 buf_ 中切出了完整的一个请求，req_ 可用，且 buf_ 里可能还有剩余字节
false：还没收齐，继续 feed

如果客户端流水线（pipelining）发送两个请求，buf_ 里会剩第二个请求的字节，reset() 之后没人再调 feed，第二个请求就被搁置。
正确写法是 while (parser.feed(...)) { handle; parser.reset(); }，但这要求 feed 在"没有新数据但 buf_ 里还有完整请求"时也能推进——即 feed("", 0) 应该能重新尝试解析 buf_
README 里应注明"未处理 HTTP pipelining，每次连接按顺序处理请求"或补上循环。


5. const HttpRequest& request() const { return req_; }
生命周期陷阱：返回的引用指向 req_，下一次 reset() 或 feed 之后可能失效（reset 会清空 req_，feed 会往里写）。
所以调用方必须在 reset() 之前用完，或者自己拷贝。


6. void reset();
解析完一个请求后，需要：
清空 req_（method/path/body/headers）；
headers_done_ = false；
content_length_ = 0；
保留 buf_（里面可能有粘包的剩余字节）。

一个连接上可能处理多个请求，需要多次 reset，析构只发生一次。


# http_parser.cpp

1. buf_.append(data, len);
用 append(const char*, size_t) 而不是 += data（const char* 版本），是因为 += 传 const char* 会按 strlen 算长度，body 里的 \0 会被截断。显式传 len 才能保证二进制安全

2. auto pos = buf_.find("\r\n\r\n");
find 从 buf_ 开头找，第一次出现的位置就是头部结束位置。npos 表示还没收齐，直接 return false 等下次
find 每次都从头扫：如果客户端把头部拆成很多小包，每来一小段就重扫一遍 buf_ 整个已收部分，复杂度是 O(n²)。
对几 KB 的头部无感；严格优化可以记一个"上次找到哪"的 search_start_

3. std::string head = buf_.substr(0, pos); // buf_.erase(0, pos + 4);
CRLF 被吃掉：substr(0, pos) 切出头部时不含 \r\n\r\n，erase(0, pos + 4) 把 4 个字节也消费掉，buf_ 剩下的正好是 body 起始


4. headers_done_ = true;
同一请求只解析一次头部。如果第一趟 feed 只收齐头部、body 不够，返回 false；下一趟 feed 进来时 headers_done_ == true，跳过整段头部解析，直接检查 body 长度。

不做这个标志位，第二次 feed 会再次 find("\r\n\r\n")——此时 \r\n\r\n 已经被 erase 掉，find 返回 npos，永远返回 false，请求卡死

5. if (!line.empty() && line.back() == '\r') line.pop_back();
为什么手动 pop_back() 去掉 \r
std::getline(iss, line) 按 \n 切行，保留行尾的 \r。而 HTTP 行尾是 \r\n，所以每个 line 末尾都带一个 \r，不去掉的话 req_.version 会变成 "HTTP/1.1\r"，字符串比较永远失败

为什么用 >> 而不是按空格切
rl >> method >> path >> version 按空白分隔，自动跳过连续空格，比手动 find(' ') 更鲁棒

没校验 version 是否为 HTTP/1.0 或 HTTP/1.1。生产级要拒绝不支持的版本

std::istringstream iss(head) 里 head 是不含 \r\n\r\n 的头部。
第一次 getline 已经读了请求行，iss 的读位置停在请求行之后，所以这个 while 从第一个 header 行开始读


为什么头部行不能用 >> 切
请求行三段用空白分隔，可以用 >>；但 header 值是可以含空格的
用 >> 会在第一个空格处截断。必须按第一个 : 切，冒号右边整段都是值


6. while (val.front() == ' ') val.erase(...) 的语义
规定 header 值前面的空白（SP/HTAB）应被剥掉，值内部和尾部的空白保留。这里只剥前导空格（没剥 \t，是个小疏漏）


7. 取 Content-Length
没有 Content-Length 时 content_length_ 保持 0
content_length_ 成员默认初始化 = 0。GET/DELETE 没 body，没有这个头，content_length_ 就是 0，
下一阶段 buf_.size() < 0 永假，直接走"body 完成"。
这让无 body 请求和空 body 请求走同一条路径，不需要特判。

8. catch (...) { return false; } 
stoul 抛 std::invalid_argument（不是数字）或 std::out_of_range（太大）。这里统一 catch (...) 返回 false
但 false 的语义是"还没收齐，下次再来"——对于"Content-Length: abc 这种永久畸形的请求，返回 false 意味着永远等不到，连接会挂住直到超时

正确做法：返回一个三态（NEED_MORE / DONE / ERROR），或者解析器里存一个 error_ 标志，让调用方区分"继续等"和"关连接回 400"。
demo 简化实现牺牲了错误处理


9. body 阶段：长度判断 + 截取
buf_.size() < content_length_ 用 < 不是 !=
buf_ 里可能多于 content_length_ 的字节——那是粘包的下一个请求。
用 < 判断"不够"、够就切前 content_length_ 字节，剩余留给下一次 feed。用 != 会在粘包时误判"没收齐"

substr(0, content_length_) 和 erase(0, content_length_) 的顺序
先 substr 拷贝出 body，再 erase 消费掉。
必须先 substr 后 erase——反过来 erase 之后 buf_ 就空了，substr 拿不到数据

return true 之后 buf_ 的状态
buf_ 里可能还有数据（粘包的下一个请求）。调用方处理完 req_ 后 reset()，reset 保留 buf_，下次 feed 能继续切。
这就是为什么 reset() 不能清 buf_


10. 没有实现:
header key 转小写、校验请求行三段非空、校验 HTTP 版本、Content-Length 非法时区分错误、
stoul 后检查尾随字符、请求大小上限 等等


# http_response.cpp

1. to_wire 与 extra_headers
它是不是"之前的 make_response" ? 功能上等价，但名字和职责划分不同
之前的 make_response 是"接收散装参数 → 拼出字符串"的一步到位
而 to_wire 是"接收一个 HttpResponse 对象 → 序列化成字节流"

        make_response(status, body, ct)	to_wire(const HttpResponse&)
输入	散装参数	一个结构体对象
职责	构造 + 序列化合一	只做序列化，构造由 handle_request 完成
扩展性	加一个新头要改函数签名	往 extra_headers 里塞就行，签名不变
状态码原因短语	通常硬编码 OK	status_text(status) 查表，201 会写 Created


2. std::unordered_map<std::string, std::string> extra_headers;
extra_headers 这个 map 是干什么的
键是 header 名，值是 header 值。例如：
resp.extra_headers["X-Request-Id"] = "abc123";
resp.extra_headers["Cache-Control"] = "no-store";
resp.extra_headers["Set-Cookie"] = "session=xyz; Path=/";
序列化后变成：
X-Request-Id: abc123
Cache-Control: no-store
Set-Cookie: session=xyz; Path=/

为什么需要它
to_wire 硬编码了 Content-Type、Content-Length、Connection 三个必需且几乎每个响应都有的头
但有些头是可选、按场景出现的
extra_headers 提供了一个开放的扩展点：常用头走固定字段，特殊头走 map，to_wire 不用改

HTTP 允许同名头重复（多个 Set-Cookie），unordered_map 会覆盖，丢失信息
生产级 HTTP 库会用 std::multimap 或 vector<pair> 保序保重


3. std::string json_escape(const std::string& s)
out.reserve(s.size() + 8);
+8 覆盖了常见的几个转义字符，避免第一次 += 时就触发扩容 (2*n+1) 
std::string 会自动扩容。只是多几次 realloc。

std::snprintf(buf, sizeof buf, "\\u%04x", c);
将字符c 转换为 Unicode 转义序列格式（\uXXXX），并安全地写入到字符数组 buf 中。

if (c < 0x20) 分支
关键：c 是 unsigned char，不是 char
std::string 的字符是 char，而 char 的符号性由实现决定（x86 上通常有符号，值域 -128~127）。
如果 c 是 char，一个 UTF-8 中文字节（如 0xE4）会被解释成 -28，
c < 0x20 比较时会发生整数提升，-28 < 32 为真，中文字节会被误判为控制字符并转义，输出乱码
用 unsigned char 强制按 0~255 解释，0xE4 就是 228，228 < 32 为假，正常走 else 原样输出

0x20 是什么
0x20 是十进制 32，即 ASCII 的空格
规定：
字符串中 U+0000 到 U+001F（即 0x00 到 0x1F）的字符必须被转义，不能直接出现在字符串字面量里。

\u%04x 的含义
\ \u 在 C 字符串里是反斜杠 + u，输出 \u。
%04x：按十六进制输出，至少 4 位，不足前面补 0。
c 是 unsigned char，会提升成 int 传给 snprintf，值域 0~255。
结果：c = 0x01 → "\u0001"；c = 0x1F → "\u001f"

else 分支
out += static_cast<char>(c);
对 ASCII 可见字符（0x20~0x7E），static_cast<char> 是无损的。
对 UTF-8 多字节字符（0x80~0xFF），static_cast<char> 把值按位原样存进 char
（可能变成负数，但字节内容不变），std::string 内部是字节容器，逐字节保留，所以 UTF-8 中文能正确通过

4. if (errno == EAGAIN || errno == EWOULDBLOCK) 
为什么这里要 poll
循环立刻回到 send，缓冲区还是满的，send 又返回 EAGAIN，又 continue……这就是 busy loop（忙等）
在 epoll 单线程服务里是灾难：这个忙等会卡死整个事件循环，其他连接全部得不到处理，服务看起来"假死"
结论：非阻塞 socket 上遇到 EAGAIN，绝对不能直接 continue。

// pollfd p{fd, POLLOUT, 0};
// if (::poll(&p, 1, 5000) <= 0) return false;

poll 做了什么
poll(&p, 1, 5000) 的语义是："阻塞当前线程，直到 fd 变成可写（发送缓冲区有空间），或 5 秒超时，或出错"。

struct pollfd {
   int fd; // 文件描述符
   short events; // 我们感兴趣的事件
   short revents; // 实际发生了的事件
};
POLLOUT：关心"可写"事件，即"发送缓冲区有空间，可以 send 了"

返回值
> 0有事件发生，p.revents 里看具体是什么（通常是 POLLOUT）。
== 0：超时，5 秒内一直不可写。
< 0：出错（errno 为 EINTR 表示被信号中断，其他是真实错误）。

poll 会让出 CPU。线程睡眠在内核里，等内核在 fd 可写时唤醒它

超时值的取舍：
太短（如 100ms）：正常但稍慢的客户端会被误杀。
太长（如 60s）：恶意连接能占用资源很久。
5 秒是常见经验值，对正常网络足够，对慢客户端能及时止损。


# todo_repo.h

todos 表的作用.
业务语义：一行 = 一个待办
id INT PRIMARY KEY AUTO_INCREMENT
主键：唯一标识一行，GET /todos/1、DELETE /todos/1 的 1 就是它
title VARCHAR(200) NOT NULL
业务数据：待办的内容，用户通过 POST 的 JSON body 传入
done TINYINT DEFAULT 0
状态标记：0 = 未完成，1 = 已完成


价值
main.cpp 完全不知道 MYSQL_ROW 是什么、row[0] 是字符串、要 std::stoi、要 ResultSet 接管 mysql_free_result
这些 C API 的细节被封装进 TodoRepo
把 MySQL 换成 SQLite，TodoRepo::list() 内部重写，main.cpp 一个字都不用改

注入防护有唯一入口
create() 里 conn_.escape(title) 是唯一把用户输入拼进 SQL 的地方

1. todo_repo = todo（待办事项）+ repo（repository，仓库）

Todo 结构体：数据在内存里的形状
表字段	Todo 字段	类型	说明
id INT AUTO_INCREMENT	id	int	主键，0 表示"还没插入"
title VARCHAR(200) NOT NULL	title	std::string	标题
done TINYINT DEFAULT 0	done	int	0 未完成、1 完成。

done 用 int 不用 bool：
MySQL 的 TINYINT 取值 -128~127，bool 只有 0/1


2. std::optional<Todo> get(int id);   // 单个，不存在返回 nullopt

3. int remove(int id);   // 返回受影响行数，-1 失败
返回三态：
> 0：删除了 n 行（正常是 1，因为 id 是主键）。
0：没找到该 id，什么都没删。
-1：SQL 失败。

为什么返回 int 而不是 bool：
> 0 → 204 No Content
0 → 404 Not Found
-1 → 500 Internal Server Error

4.
为什么是 get(int id) 而不是 get(int& id)
C++ 的惯例：函数不修改的参数，不要用非 const 引用
int& 不能传字面量和临时值
int 是 4 字节，值传递和引用传递成本一样，甚至更快
引用的价值在大对象（std::string、std::vector、自定义结构体）——避免拷贝整个对象


# todo_repo.cpp

1. row_to_todo
参数 MYSQL_ROW row 为什么按值传
MYSQL_ROW 的类型是 typedef char **MYSQL_ROW;，本质是指针（8 字节）。
指针按值传就是拷 8 字节，和引用传指针的开销一样，没必要用 const MYSQL_ROW&
更重要的是：row 指向的是 MYSQL_RES 内部缓冲区，函数不拥有它，也不修改它指向的指针数组

if (row[i])：判 NULL 指针，不是判空字符串
row[i] 是 char*
当数据库里该列的值是 SQL NULL 时，MySQL C API 返回的 row[i] 是 nullptr，不是字符串 "NULL"，也不是空串 ""

row_to_todo 是 list 和 get 的共享转换逻辑。没有它，两处都要写一遍 if (row[0]) ... stoi ...

row_to_todo 的契约：调用方必须保证列顺序
要求所有调用方的 SQL 都按 SELECT id, title, done 的顺序。


2. std::vector<Todo> TodoRepo::list()
为什么 list() 返回 std::vector<Todo>
如果直接返回 C API 的裸指针：MYSQL_RES* list();
调用方 main.cpp 就要自己承担：
MYSQL_RES 的生命周期管理（必须 mysql_free_result）；
逐行 mysql_fetch_row；等等
Repository 就白封装了。
vector 在连续遍历上是所有容器里最快的

3. create 的注意点
mysql_insert_id 返回 uint64_t，转 int 会截断
mysql_insert_id 必须在 execute 成功后立即调用
title 为空：Repo 层没校验
title 超过 VARCHAR(200) 会被 MySQL 截断或报错
execute 失败时，last_error 可能有更细的错误码

4. remove 的注意点
affected_rows 返回 uint64_t，转 int 同样截断
id 是 int，但没校验负数/0
affected_rows == 0 的语义：没找到，不是失败


没有事务、没有连接重连、没有 SQL 日志


# main.cpp


## JSON 手写 输出侧和输入侧是两件事

1. 输出侧：json_escape + 拼接
MySQL 返回的 row[1]（title）是任意字符串，可能含：
字符	不转义时 JSON 会怎样
"	提前结束字符串，JSON 非法
\	把后一个字符当转义，语义错乱
\n / \r / \t	JSON 字符串里不允许裸控制字符，解析器报错
其他 < 0x20 的控制字符	同上

UTF-8 中文不需要转义

2.  输入侧：从 {"title":"..."} 提取 title

3. "\"" + key + "\"" 拼出什么
"\""      →  "
key       →  title
"\""      →  "
─────────────────
结果       →  "title"     （含首尾引号，共 7 个字符）

为什么必须带引号：否则会误匹配
body 是 {"title_extra":"x","title":"real"} 时，
find("title") 会匹配到 title_extra 里的 title 子串（位置 2），然后后续找 :、找引号，取到的是 _extra 之后的东西，完全错位

漏洞 A：值里包含 "title" 字面量
漏洞 B：key 是别的键的值的子串
漏洞 C：不处理嵌套、数组、多行


## “路由”和“路径参数”

1. 路由
路由（routing）= 把"进来的请求"映射到"该执行的处理逻辑"。

HTTP 服务的每个请求都由两个东西唯一确定：
维度	例子	作用
method	GET / POST / DELETE	表达“想做什么”
path	/ / /todos / /todos/1	表达“对谁做”
路由 = 根据 (method, path) 这个二元组，找到对应的处理函数。

method	path	调哪个处理函数
GET	/	serve_index() → 返回 index.html
GET	/todos	list_todos() → 查所有 todo
POST	/todos	create_todo(body) → 插入一条
GET	/todos/:id	get_todo(id) → 查单个
DELETE	/todos/:id	delete_todo(id) → 删单个


static HttpResponse handle_request(const HttpRequest& req,
                                   TodoRepo& repo,
                                   const std::string& index_html) 
req	解析好的请求（method、path、body、headers）	HttpParser 解析出来
repo	数据访问层	main 里的 TodoRepo 对象
index_html	静态页内容	启动时读进内存

返回：一个 HttpResponse（状态码 + content-type + body + 额外头），由 main 交给 to_wire 序列化成字节流发出去

内部结构：一个大 if-else 分发树
分支 ①：GET / → 返回静态页
分支 ②：/todos 集合
分支 ③：/todos/:id 单个资源
分支 ④：兜底 404

必须精确匹配在前。更稳妥的做法是从最具体到最宽泛排列。大小写敏感。


# 整体图解
┌─────────────────────────────────────┐
│  main.cpp（HTTP 层）                 │  ← 处理请求、路由、JSON 序列化
│    handle_request(req, repo, ...)   │
└──────────────┬──────────────────────┘
               │ 调用 repo.list() / repo.create()
               ▼
┌─────────────────────────────────────┐
│  TodoRepo（数据访问层 / Repository）  │  ← 只写 SQL、取数据、转成 Todo 结构
│    list() / get() / create()        │
└──────────────┬──────────────────────┘
               │ 调用 conn_.execute() / conn_.query()
               ▼
┌─────────────────────────────────────┐
│  MySQLConnection（连接封装层）        │  ← RAII、escape、错误信息
│    execute / query / escape         │
└──────────────┬──────────────────────┘
               │ mysql_query / mysql_store_result
               ▼
┌─────────────────────────────────────┐
│  MySQL C API（libmysqlclient）       │
└─────────────────────────────────────┘