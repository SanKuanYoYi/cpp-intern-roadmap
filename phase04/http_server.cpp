#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

// 静态文件根目录（相对进程工作目录，必须在 index.html 所在目录启动服务器）
static const std::string ROOT = ".";

// ------------------------------------------------------------------
// 读整个文件到 std::string，失败返回 false
// ------------------------------------------------------------------
static bool read_file(const std::string &path, std::string &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;

  std::ostringstream ss;
  ss << in.rdbuf(); // 一次读到 EOF
  if (in.bad())
    return false; // 真正的读错误
  // ifs.fail() && !ifs.eof() 也可能表示错误，但 rdbuf 场景下通常会到 eof
  out = ss.str();
  return true;
}

// ------------------------------------------------------------------
// 循环发完所有字节（send 可能部分发送，必须循环）
// ------------------------------------------------------------------
static bool send_all(int fd, const std::string &data) {
  ssize_t total = 0;
  while (total < data.size()) {
    ssize_t n =
        send(fd, data.data() + total, data.size() - total, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (n == 0)
      return false;
    total += n;
  }
  return true;
}

// ------------------------------------------------------------------
// 构造完整 HTTP 响应（头部 + body）
// ------------------------------------------------------------------
static std::string make_response(int code, const std::string &status,
                                 const std::string &content_type,
                                 const std::string &body) {
  std::ostringstream ss;
  ss << "HTTP/1.1 " << code << " " << status << "\r\n";
  ss << "Content-Type: " << content_type << "\r\n";
  ss << "Content-Length: " << body.size() << "\r\n";
  ss << "Connection: close\r\n";
  ss << "\r\n";
  ss << body;
  return ss.str();
}

// ------------------------------------------------------------------
// 处理一个客户端连接
// ------------------------------------------------------------------
static void handle_client(int connfd) {
  // ---------- 1. 读到 \r\n\r\n 为止（请求头结束标志） ----------
  // HTTP 请求可能在 TCP 上被拆成多个段，必须循环 recv
  std::string req; // ① 累积容器（跨多次 recv）
  char buf[1024];  // ② 单次接收缓冲（每次 recv 覆盖）
  while (req.find("\r\n\r\n") == std::string::npos) {
    ssize_t n = recv(connfd, buf, sizeof(buf), 0);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      perror("recv");
      close(connfd);
      return;
    }
    if (n == 0) {
      close(connfd);
      return;
    }
    req.append(buf, n);
    if (req.size() > 65536)
      break; // 防御：请求头异常大
  }

  // ---------- 2. 解析请求行 ----------
  size_t line_end = req.find("\r\n");
  if (line_end == std::string::npos) {
    send_all(connfd,
             make_response(400, "Bad Request", "text/plain", "bad request\n"));
    close(connfd);
    return;
  }
  std::string request_line = req.substr(0, line_end);
  printf("[http] request line: %s\n", request_line.c_str());

  std::istringstream iss(request_line);
  std::string method, path, version;
  iss >> method >> path >> version;
  if (method.empty() || path.empty() || version.empty()) {
    send_all(connfd, make_response(400, "Bad Request", "text/plain",
                                   "bad request line\n"));
    close(connfd);
    return;
  }

  // ---------- 3. 只支持 GET ----------
  if (method != "GET") {
    send_all(connfd, make_response(405, "Method Not Allowed", "text/plain",
                                   "method not allowed\n"));
    close(connfd);
    return;
  }

  // ---------- 4. 路由 ----------
  if (path == "/")
    path = "/index.html";

  // 安全：禁止 ../目录穿越。demo 演示
  if (path.find("..") != std::string::npos) {
    send_all(connfd,
             make_response(403, "Forbidden", "text/plain", "forbidden\n"));
    close(connfd);
    return;
  }

  // ---------- 5. 读文件 ----------
  std::string filepath = ROOT + path;
  std::string body;
  if (!read_file(filepath, body)) {
    send_all(connfd,
             make_response(404, "Not Found", "text/plain", "404 not found\n"));
    close(connfd);
    printf("[http] 404 %s\n", path.c_str());
    return;
  }

  // ---------- 6. 200 OK ----------
  // demo 简化：所有文件都当 html 返回（生产要后缀查）
  std::string resp = make_response(200, "OK", "text/html;charset=utf-8", body);
  send_all(connfd, resp);
  close(connfd);
  printf("[http] 200 %s (%zu bytes)\n", path.c_str(), body.size());
}

int main() {
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    perror("socket");
    return 1;
  }

  int opt = 1;
  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("getsockopt");
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(8080);

  if (bind(listenfd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) <
      0) {
    perror("bind");
    close(listenfd);
    return 1;
  }

  if (listen(listenfd, 128) < 0) {
    perror("listen");
    close(listenfd);
    return 1;
  }
  printf("[http] listening on 0.0.0.0:8080, root=%s\n", ROOT.c_str());

  // 单线程串行处理（demo 够用；
  while (true) {
    int connfd;
    do {
      connfd = accept(listenfd, nullptr, nullptr);
    } while (connfd < 0 && errno == EINTR);

    if (connfd < 0) {
      perror("accept");
      break;
    }
    std::thread(handle_client, connfd).detach();
  }

  close(listenfd);
  return 0;
}

/*
AI 解析文档：

static const std::string ROOT = ".";
定义"静态文件的根目录"——所有请求的 URL 路径都会拼在它后面，变成实际的文件路径
. —— 当前工作目录（cwd）
如果改成 ROOT = "/var/www/html"：
这就叫"静态文件根目录"

std::string —— 为什么不用 const char*
std::string 天然支持 + 拼接、+= 追加、.find() 查找等——char*
只是个指针，这些操作都没有。 既然要拼路径，用 std::string 是自然选择。

static 在这里不是"静态存储期"（std::string
本来就是全局生命周期），而是"内部链接（internal linkage）"—— 这个变量只在当前
.cpp 文件内可见，其他文件看不到

为什么不直接用绝对路径
因为绝对路径写死在代码里，换机器/换目录就跑不了：


ss << ifs.rdbuf(); out = ss.str();
把整个文件内容一次性"流式拷贝"到内存字符串里——ifs.rdbuf()
拿到文件流的底层缓冲区，
<< 把缓冲区全部内容搬到 ostringstream，str() 转成 std::string

① ifs.rdbuf() —— 拿到"底层的流缓冲区"
rdbuf() 返回 std::filebuf*——它代表"这个流背后真正的数据源"。
② ss << ifs.rdbuf() —— 重载的"流拷贝"
// 伪代码，<ostream> 里类似这样
// ostream& operator<<(ostream& os, std::streambuf* sb);
它的语义是："从 sb 这个缓冲区一直读到 EOF，把读到的每个字节都 os.put() 进去"。
③ ss.str() —— 把 ostringstream 的内容转成 std::string
std::ostringstream 内部维护一个动态增长的字符缓冲区。.str() 返回一份拷贝
注意：.str() 是值返回（拷贝），对于大文件可能有一次完整的内存拷贝。
C++20 有 .str() && 移动版本，但 demo 里是 C++11 兼容写法。

传统写法 1：seekg/tellg/read
//std::ifstream ifs(path, std::ios::binary);
//if (!ifs) return false;
//ifs.seekg(0, std::ios::end);       // 定位到文件末尾
//std::streamsize size = ifs.tellg(); // 拿到文件大小
//ifs.seekg(0, std::ios::beg);       // 回到开头
//out.resize(size);                  // 预分配
//ifs.read(&out[0], size);           // 一次读满

传统写法 2：循环 getline / read
//char buf[4096];
//while (ifs.read(buf, sizeof(buf))) {
//   out.append(buf, ifs.gcount());
//}
//out.append(buf, ifs.gcount());  // 处理最后不满一块的


make_response() 详解 —— HTTP 响应的"装配车间"
把"状态码 + 状态短语 + 头部 + body"按 HTTP/1.1
文本协议格式拼成一个完整字节串，返回给调用方去 send static std::string

make_response(int code, const std::string& status, const std::string&
content_type, const std::string& body); ① code 和 status 为什么分开传 HTTP
状态行是： HTTP/1.1 200 OK\r\n ↑   ↑ code  status（reason phrase） 理论上 code
和 status 是绑定的——200 就该是 OK，404 就该是 Not Found。 为什么不直接传
code，函数内部查表？

demo 里只用到 4 个状态码（200/400/403/404/405），写死查表反而更啰嗦：
static const std::map<int, std::string> REASONS = {
    {200, "OK"}, {400, "Bad Request"}, {403, "Forbidden"},
    {404, "Not Found"}, {405, "Method Not Allowed"},
};没必要为一个 demo 引 map 和查表。
HTTP 规范允许 reason phrase 任意——它只是给人看的，协议语义只看 code

② content_type 为什么传入而不是内部推导
理想情况：服务器应该根据文件后缀自动选 Content-Type：
但代码里是调用方传的——因为：
405/404 这类错误页面没有"文件后缀"，需要固定 text/plain
demo 把"文件类型推导"简化掉了——所有文件都当 text/html 返回

生产级实现会有个 mime_type(path) 函数，按后缀查表返回 MIME。demo 省略了。

③ body 传 const std::string& —— 零拷贝
HTML 文件可能几 MB，每次响应多拷贝一次是不必要的浪费。


为什么用 ostringstream 而不是 std::string +？
除了上面说过的
生产 HTTP 服务器会更底层，但 demo 用 ostringstream 是可读性和安全的平衡。

协议要求：
HTTP-version SP status-code SP reason-phrase CRLF
HTTP/1.1 —— 协议版本。必须大写
SP（单空格） —— 只能是一个空格
200 —— 状态码，3 位数字
OK —— reason phrase，可以是空字符串，但不推荐
\r\n —— CRLF，不是单独的 \n
HTTP/1.1 规范明文要求 CRLF（\r\n）

Windows	            CRLF                (\r\n)
Unix/Linux/macOS	LF (\n)         	简化为单字符换行
Mac OS          	CR (\r)	            早期苹果系统使用

ss << "Content-Type: " << content_type << "\r\n";
Content-Type	浏览器行为
text/html	当 HTML 解析渲染
text/plain	当纯文本显示（不解析标签）
image/png	当图片显示
application/json	不显示，交给 JS 处理
application/octet-stream	下载，不显示

代码里传的是：
"text/html; charset=utf-8"    // 200 响应
"text/plain"                   // 404/405 响应
charset=utf-8 至关重要——中文页面的命根子

text/html 和 text/html; charset=utf-8 的区别：
前者只有类型，编码靠浏览器猜（或 <meta>）；
后者明确编码，更可靠、更专业。

Content-Length
ss << "Content-Length: " << body.size() << "\r\n";  // ⚠ 必须是 body
的真实字节数 body.size() 是
std::string::size()——返回字节数（size_t），不是"字符数"。

场景	body.size()	Content-Length 应填	常见错误
ASCII 文本 "hello"	5	5	✅
中文 "你好"（UTF-8）	6	6	❌ 填 2（按字符数）
含 \r\n 的文件	实际字节	实际字节	❌ 在 Windows 上被文本模式转换
二进制文件（图片）	文件字节数	文件字节数	❌ 用 strlen() 遇到 \0
截断

Content-Length 填大了 → 客户端等不到足够数据 → 超时、连接卡死；
Content-Length 填小了 → 客户端只读前 N 字节 →
内容截断（表现为"页面显示一半"、"图片坏掉"） 完全没写 →
客户端靠连接关闭判断结束，浏览器看不到进度、无法判断完整性。 不要用
strlen(body.c_str())——遇到 \0 会截断。

ss << "Connection: close\r\n";
告诉客户端："这条连接只服务这一个请求，响应完就关"。
每个请求一次 TCP 握手。demo 场景下无所谓
生产情景
Connection: keep-alive    ← 声明复用（HTTP/1.1 默认）
Connection: Upgrade       ← 协议升级（如 WebSocket）


空行 —— 一个字节都不能少
ss << "\r\n";   // 空行：头部结束
这一个 \r\n 是 HTTP 响应的"命门"：
HTTP/1.1 200 OK\r\n           ← 状态行结束
Content-Type: text/html\r\n    ← 每个头结束
Content-Length: 15\r\n
Connection: close\r\n
\r\n                            ← 关键：空行 = 头部结束、body 开始
<h1>你好</h1>                   ← body

果少写这个 \r\n 会怎样：
客户端会认为 body 还没开始——它继续把 <h1>你好</h1> 解析成"头部的延续"；
头部里有非 Name:  解析失败 → 报错；
curl 报错：Malformed HTTP response；
浏览器：ERR_INVALID_HTTP_RESPONSE 或页面空白。

return ss.str();
把 ostringstream 的内部缓冲拷贝成 std::string 返回。
C++20 有移动版

为什么要"先构造再发"而不是边拼边发
Content-Length 要先算出来——如果不构造完整响应，Content-Length 和实际 body
分开两次写，容易不一致； 一次 send 系统调用 vs
五次——系统调用有开销，批量发减少上下文切换； 错误处理集中；  TCP 层面更友好


Date、Server 头
RFC 7231："服务器应该发送 Date
头，除非没有时钟"——"应该"不是"必须"，浏览器能处理缺失 Server 头：可选 超出 demo
范围，代码故意简化了。

生产级改进
Content-Type 应该由路径推导
Connection: close 应可配置
Content-Length 对"流式响应"的限制
超出 demo 范围


buf 和 req —— 两个完全不同的角色
std::string req;        // ① 累积容器（跨多次 recv）
char buf[1024];         // ② 单次接收缓冲（每次 recv 覆盖）

循环 recv
tcp_server_basic 是 demo——客户端发一条短消息，一次 recv
就能拿全（假设，可能出错但 demo 不暴露）； http_server
是真实协议——请求可能被拆，必须循环。

buf 的用法也不同：
之前：sizeof(buf) - 1（要留位给 buf[n] = '\0'）；
现在：sizeof(buf)（不用 \0，因为 req.append(buf, n) 按长度处理）。


req.append(buf, n) —— 为什么带长度 n
这是"按长度追加 n 字节"的重载：
std::string& append(const char* s, size_type n);
// 从 s 处拷贝 n 字节追加到末尾

为什么不用更简单的 req.append(buf)：
// ❌ 危险
req.append(buf);
append(const char* s) 这个重载依赖 \0 终止——它会一直读到 \0 才停。但 buf 没有 \0
终止符！

正确做法：显式告诉 append "只追加前 n 个字节"——req.append(buf, n)。

这也是为什么 recv 用 sizeof(buf) 而不是 sizeof(buf) - 1：
因为 append(buf, n) 不依赖 \0——buf 里是什么字节都无所谓，只要告诉 append 长度。
这和 tcp_server_basic 里的 recv(connfd, buf, sizeof(buf)-1, 0) 形成对比——
那里要 buf[n] = '\0' 当字符串用，所以必须留位。

当 C 字符串用	sizeof(buf) - 1	buf[n] = '\0'
按长度处理	sizeof(buf)	append(buf, n) 或 write(fd, buf, n)

HTTP 服务器是后者——因为它最终要把完整请求放进 std::string req，std::string
天然按长度管理，不依赖 \0。


std::string request_line = req.substr(0, line_end);
为什么"每次"都从 0
因为每个 handle_client 只处理一条连接、一个请求——req
每次都是新的，请求行永远在开头。 如果服务器支持
keep-alive（一条连接处理多个请求），那么第二个请求的请求行确实不在 req 的第 0
字节—— 那时候就需要维护一个"当前解析位置"的偏移量 demo 用 Connection:
close——一连接一请求，所以永远从 0 开始。

iss >> method >> path >> version 的规则
std::istringstream 的 operator>> 是格式化提取运算符。对 std::string
目标，它的规则是： ① 跳过前导空白 → ② 读取非空白字符直到下一个空白或流结束 → ③
把读到的内容给目标

空白（whitespace） 由 std::isspace() 定义，包括：
' '（空格）	✅
'\t'（Tab）	✅
'\n'（换行 LF）	✅
'\r'（回车 CR）	✅
'\v'（垂直 Tab）	✅
'\f'（换页）	✅
所以 \r 和 \n 都是空白——>> 遇到它们会像遇到空格一样停止读取。

method = "GET"
path = "/index.html"
version = "HTTP/1.1"

关键点：\r\n 不在 request_line 里
request_line = req.substr(0, line_end) 已经把 \r\n 切掉了：
所以 >> version 读的是 "HTTP/1.1"

如果不做 substr 会发生什么
req 是 "GET /index.html HTTP/1.1\r\nHost: ...\r\n\r\n"：
流里还有大量内容没读完：
所以 substr 是必须的——它把"请求行"和"请求头"分开，让 >>
只处理请求行那一小块干净的字符串

>> 遇到 \0 不认为是空白——会当成普通字符读入。
但 HTTP 请求行里有 \0 本身是非法输入，>> 之后解析出来的内容会带
\0，后面路由匹配会失败， 最终返回 404 或 400。

>> 和 getline 的差异
维度	iss >> token	std::getline(iss, line)
分隔符	任意空白（空格、Tab、\n、\r）	只按 \n 切
\r	当空白跳过	保留在字符串末尾
连续空白	自动跳过（多个当分隔）	保留
结果	一个 token	一整行（不含 \n，含 \r）
用途	解析"空格分隔的字段"（请求行）	解析"一行文本"（请求头每行）
// 要手动去掉 \r


路由 + 安全检查

第一段：if (path == "/") path = "/index.html";
浏览器/curl 访问 http://127.0.0.1:8080/ 时，请求行是：
GET / HTTP/1.1\r\n
    ↑
  path 就是 "/"
/ 本身不是一个文件——它是"网站根路径"。如果直接 ROOT + "/" = "./"，
std::ifstream("./") 打不开目录（ifstream 不能读目录），会返回 404。

用户输入 http://127.0.0.1:8080/ 看到 404，但输入
http://127.0.0.1:8080/index.html 正常

所以要把 / 映射到默认首页：
GET / HTTP/1.1        →  path = "/"  →  改成 "/index.html"  →  打开 ./index.html
GET /index.html       →  path = "/index.html"  →  不动  →  打开 ./index.html
这是 Web 服务器的通行惯例
用 curl -v / 和 curl -v /index.html 两次，结果完全一样——这就是这行代码的作用。

为什么是 /index.html 而不是别的
因为测试文件就叫 index.html——这是约定俗成的命名
生产服务器会按顺序试多个 demo 简化成"只看 index.html"

第二段：if (path.find("..") != std::string::npos)
攻击者发：
//GET /../../../etc/passwd HTTP/1.1
path = "/../../../etc/passwd"，filepath = ROOT + path：
//ROOT = "."
//filepath = "." + "/../../../etc/passwd" = "./../../../etc/passwd"

./../.. 的语义是"向上跳两级"——从当前目录往上走：
会成功打开 /etc/passwd——攻击者读到了系统用户列表。

path.find("..")     // 在 path 里找 ".." 这个子串
std::string::npos	没找到 .. → 安全
其他值（0、5 等）	找到了 .. 的下标 → 拒绝

拦不住的边缘情况（demo 里不管，生产要注意）
① URL 编码的 ..
攻击者可能用 %2e%2e 代替 ..：
② 绝对路径（path 以 / 开头）
③ 符号链接      ./link → /etc/passwd
④ Windows 的反斜杠      Windows 上 ..\ 也是上级目录：
demo 的威胁模型是"演示概念"。生产 HTTP 服务器必须做完整的路径规范化。
*/

//****************************************************************//

/*
运行结果：

# 终端 1
./http_server
# [http] listening on 0.0.0.0:8080, root=.

# 终端 2
curl -v http://127.0.0.1:8080/index.html
# < HTTP/1.1 200 OK
# < Content-Type: text/html; charset=utf-8
# < Content-Length: 234
# < Connection: close
# <
# <!DOCTYPE html>...

curl -v http://127.0.0.1:8080/
# 同 200（路由把 / 映射到 /index.html）

curl -v http://127.0.0.1:8080/nonexist.html
# < HTTP/1.1 404 Not Found
# < Content-Length: 14

curl -v -X POST http://127.0.0.1:8080/index.html
# < HTTP/1.1 405 Method Not Allowed
*/

/*
AI 解析:

HTTP 请求的格式是什么？
请求行\r\n
请求头1: 值\r\n
请求头2: 值\r\n
...
\r\n                       ← 空行（一个孤立的 \r\n）
请求体                       ← 可选，GET 一般为空

① 请求行（Request Line）——第一行
GET /index.html HTTP/1.1\r\n
└┬┘ └────┬─────┘ └──┬───┘
方法     请求目标    HTTP 版本
三个字段用单个空格（SP）分隔，不能是多个空格或 Tab（严格实现会拒绝）。
方法：GET、POST、PUT、DELETE、HEAD、OPTIONS…
请求目标：通常是一个路径 /index.html，也可能是绝对 URL（代理场景）
版本：HTTP/1.0、HTTP/1.1、HTTP/2（HTTP/2 是二进制，不用文本请求行）

② 请求头（Headers）——多行
Host: 127.0.0.1:8080\r\n
User-Agent: curl/8.5.0\r\n
Accept: *//*\r\n
Content-Length: 42\r\n
名称大小写不敏感：
值大小写敏感
同一头可以出现多次
冒号后允许一个空格

③ 空行 —— 一个孤立的 \r\n
这是 HTTP 文本协议最关键的一个字节序列——它标记"头部结束、body 开始"。

④ 请求体 —— 可选
GET：绝大多数情况没有 body（虽然协议不禁止）；
POST / PUT / PATCH：body 是提交的数据（表单、JSON、文件）；


Content-Length 有什么用？不写会怎样？
作用：声明 body 的字节数，让接收方知道"消息在哪里结束"

不写 Content-Length 会怎样

情形 1：响应中不写，Connection: close
客户端读到连接关闭才知道 body 结束：
能工作，但代价大：
每条响应必须关连接，无法复用；
客户端无法知道下载进度（不知道总大小）；
无法判断完整性——如果服务器中途崩溃，客户端看到的是"连接关了但内容不完整"，可能误以为成功。

情形 2：响应中不写，Connection: keep-alive
HTTP/1.1 规定这种响应是非法的——连接还要复用，body 又没边界，客户端根本不知道下一条响应从哪里开始。

情形 3：请求中不写（POST 等）
服务器不知道请求体多长：


为什么 HTTP 服务器通常用 Connection: keep-alive？你的服务器为什么用 close？
keep-alive 的好处
一次 TCP 连接处理多个 HTTP 请求/响应，而不是"一个请求一个 TCP 连接"：
省去反复三次握手
避免 TCP 慢启动
减少服务端 accept 和 close 次数——减轻内核调度
HTTP/1.1 默认 keep-alive——Connection: close 才是要显式写的例外

为什么 demo 服务器用 close
keep-alive 需要	  demo
精确解析每条请求的边界（Content-Length 或 chunked）	❌ 只读到 \r\n\r\n，不处理 body
循环处理同一连接上多个请求	❌ 一个连接处理一次就 close
客户端没发完请求时继续等	❌ 读完一次就 send
空闲超时（客户端保持连接不发请求多久后踢掉）	❌ 没实现，keep-alive 会 fd 泄漏
流式响应 / 分块响应	❌ 全在内存里拼好发一次

Connection: close 是"一种更简单的协议模式"——一条连接只服务一个请求


浏览器除了 GET /index.html 还会发什么？
访问 http://127.0.0.1:8080/index.html 后，浏览器会自动发第二个请求
是标签页的小图标。如果服务器没有这个文件，会返回 404
*/