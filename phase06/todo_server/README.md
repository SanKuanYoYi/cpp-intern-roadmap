# todo_server
一个用 C++17 + epoll + MySQL 实现的极简 Todo HTTP 服务，单线程事件循环、手写 HTTP/1.1 解析、手写 JSON 序列化，不依赖任何第三方 HTTP/JSON 库。

---

## 技术栈

| 组件 | 说明 |
|---|---|
| C++17 | `std::optional`、`std::string_view` 风格、RAII |
| epoll | 边缘触发（EPOLLET）单线程事件循环，方案 A |
| MySQL | 通过 `libmysqlclient` C API，封装为 `MySQLConnection` + `ResultSet` RAII |

**并发模型（方案 A）**：整个服务只有一个线程，`epoll` 串行处理所有连接；MySQL 只有一个 `MySQLConnection`，由 `TodoRepo` 持有引用。单线程下不存在 `MYSQL*` 的数据竞争，**不需要加锁**。

---

## 构造

依赖：
```bash
# Debian / Ubuntu
sudo apt install build-essential cmake pkg-config libmysqlclient-dev

# macOS (Homebrew)
brew install cmake pkg-config mysql-client
export PKG_CONFIG_PATH="$(brew --prefix mysql-client)/lib/pkgconfig:$PKG_CONFIG_PATH"
```

构建：
```bash
cd phase06/todo_server
mkdir -p build && cd build
cmake ..
make
```
产物：build/todo_server


运行
1. 启动 MySQL 并建库
```bash
# 确认 MySQL 服务在跑
sudo systemctl start mysql      # Linux
brew services start mysql       # macOS

# 建库（表由程序启动时自动创建）
mysql -u root -p -e "CREATE DATABASE IF NOT EXISTS intern_prep CHARACTER SET utf8mb4;"
```

程序启动时会执行 CREATE TABLE IF NOT EXISTS todos (...)，不需要手动建表。表结构：
```bash
CREATE TABLE todos (
    id INT PRIMARY KEY AUTO_INCREMENT,
    title VARCHAR(200) NOT NULL,
    done TINYINT DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

2. 修改连接参数
main.cpp 顶部：
```cpp
MySQLConnection conn("localhost", "root", "你的密码", "intern_prep");
```
把 "你的密码" 换成本机 MySQL 的真实密码。不要把真实密码提交到 git，练习时可临时硬编码，提交前改回占位符或用环境变量读取。


3. 启动
```bash
# 注意：index.html 要在当前工作目录
cp ../index.html .
./todo_server
# Server is running on http://localhost:8080
```

4. 验证
```bash
curl -v http://127.0.0.1:8080/
```


API 列表
所有响应都带 Connection: close，每个连接只处理一个请求，响应发完即关闭。

|方法|	路径|	请求体|	成功响应|	状态码|
|---|---|---|---|---|
GET	|/	|无|	index.html| 内容|	200|
GET	|/todos|	无|	JSON 数组|	200|
POST|	/todos|	{"title":"..."}	|{"id":N,"title":"...","done":0}|	201|
GET	|/todos/:id|	无|	{"id":N,"title":"...","done":0}|	200|
DELETE|	/todos/:id|	无|	空|	204|

错误响应：
状态码	场景	响应体
400	POST /todos 缺 title / 为空	{"error":"Missing title"}
404	GET /todos/:id 不存在、DELETE /todos/:id 不存在、未知路径	{"error":"Todo not found"} / {"error":"Not Found"}
405	路径存在但方法不支持	{"error":"Method Not Allowed"}
500	SQL 执行失败	{"error":"Failed to create todo"} / {"error":"Failed to delete todo"}

响应头：
GET / → Content-Type: text/html; charset=utf-8
成功 JSON → Content-Type: application/json; charset=utf-8
错误 JSON → Content-Type: text/plain; charset=utf-8（body 仍是 JSON 文本，已知限制见下文）



curl 示例与实际输出
以下输出为实际运行时的格式示例，Date、Content-Length 等具体值以本机为准。

1. GET / — 返回首页
```bash
curl -v http://127.0.0.1:8080/
```

```text
> GET / HTTP/1.1
> Host: 127.0.0.1:8080
> User-Agent: curl/8.x
> Accept: */*
>
< HTTP/1.1 200 OK
< Content-Type: text/html; charset=utf-8
< Content-Length: 123
< Connection: close
<
<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><title>Todo</title></head>
<body>
  <h1>Todo Server</h1>
  <p>API: GET /todos, POST /todos, GET /todos/:id, DELETE /todos/:id</p>
</body>
</html>
```

2. POST /todos — 创建
```bash
curl -v -X POST http://127.0.0.1:8080/todos \
     -H "Content-Type: application/json" \
     -d '{"title":"learn epoll"}'
```

```text
> POST /todos HTTP/1.1
> Host: 127.0.0.1:8080
> Content-Type: application/json
> Content-Length: 23
>
< HTTP/1.1 201 Created
< Content-Type: application/json; charset=utf-8
< Content-Length: 42
< Connection: close
<
{"id":1,"title":"learn epoll","done":0}
```

缺 title 时：
```bash
curl -X POST http://127.0.0.1:8080/todos \
     -H "Content-Type: application/json" \
     -d '{}'
# 400
# {"error":"Missing title"}
```


3. GET /todos — 列表
```bash
curl -v http://127.0.0.1:8080/todos
```

```text
< HTTP/1.1 200 OK
< Content-Type: application/json; charset=utf-8
< Content-Length: 43
< Connection: close
<
[{"id":1,"title":"learn epoll","done":0}]
```
表为空时返回 []。

4. GET /todos/:id — 单个
```bash
curl -v http://127.0.0.1:8080/todos/1
```

```text
< HTTP/1.1 200 OK
< Content-Type: application/json; charset=utf-8
<
{"id":1,"title":"learn epoll","done":0}
```

不存在时：
```bash
curl -v http://127.0.0.1:8080/todos/999
# 404
# {"error":"Todo not found"}
```


5. DELETE /todos/:id — 删除
```bash
curl -v -X DELETE http://127.0.0.1:8080/todos/1
```

```text
< HTTP/1.1 204 No Content
< Content-Type: text/plain; charset=utf-8
< Content-Length: 0
< Connection: close
<
```

不存在时：
```bash
curl -X DELETE http://127.0.0.1:8080/todos/999
# 404
# {"error":"Todo not found"}
```

6. 方法不支持
```bash
curl -v -X PUT http://127.0.0.1:8080/todos
# 405
# {"error":"Method Not Allowed"}
```

7. 数据库验证
```bash
mysql -u root -p intern_prep -e "SELECT * FROM todos;"
```

```text
+----+-------------+------+---------------------+
| id | title       | done | created_at          |
+----+-------------+------+---------------------+
|  1 | learn epoll |    0 | 2026-11-07 10:00:00 |
+----+-------------+------+---------------------+
```


SQL 注入验证
POST /todos 内部用 MySQLConnection::escape()（封装 mysql_real_escape_string）转义 title，注入字符串只会作为普通标题插入，不会执行。

正确的 curl 写法（bash 下单引号会提前闭合，必须用 --data-raw + 双引号 + \" 转义，或 $'...'）：
```bash
curl -v -X POST http://127.0.0.1:8080/todos \
     -H "Content-Type: application/json" \
     --data-raw "{\"title\":\"'; DROP TABLE todos;--\"}"
```

预期：
```text
< HTTP/1.1 201 Created
<
{"id":2,"title":"'; DROP TABLE todos;--","done":0}
```

验证表还在：
```bash
mysql -u root -p intern_prep -e "SHOW TABLES LIKE 'todos'; SELECT * FROM todos;"
```

```text
+-------------------+
| Tables_in_intern_prep (todos) |
+-------------------+
| todos             |
+-------------------+
+----+--------------------------+------+
| id | title                    | done |
+----+--------------------------+------+
|  2 | '; DROP TABLE todos;--   |    0 |
+----+--------------------------+------+
```
注意：escape() 的转义规则依赖连接的 character_set_connection，main.cpp 启动时执行了 SET NAMES utf8mb4，这是中文和特殊字符正确转义的前提，不要删除。


## 已知限制
HTTP 协议层
不支持 HTTP pipelining：一次 recv 如果包含两个请求，当前代码只处理第一个，随后 close 连接，第二个请求的字节丢失。客户端应等上一个响应完全收到后再发下一个请求。

不支持 keep-alive：响应固定带 Connection: close，每个请求一个 TCP 连接，发完即关。高并发下三次握手开销比 keep-alive 大。

只支持 Content-Length，不支持 Transfer-Encoding: chunked：chunked 请求的 body 会被当成空 body 处理。curl -d 默认发 Content-Length，正常使用不受影响。

header 名大小写敏感：只认 Content-Length，客户端发 content-length 会解析不到，body 被当成空。

Content-Length 解析失败会永久等待：std::stoul 抛异常时 feed 返回 false，但语义是"还没收齐"，连接会一直挂着直到客户端超时。严格的实现应返回三态（需要更多 / 完成 / 错误），错误时关闭连接。

path 未做 URL-decode，未剥离 query string：GET /todos/1?x=2 的 path 是 /todos/1?x=2，substr(7) 得到 1?x=2，std::stoi 解析到 1 后停止，但当前代码没有检查尾随字符，会当成 id=1 处理；如果 query 以非数字开头，stoi 抛异常。

无请求大小上限：恶意客户端可以发巨大的 Content-Length 并慢慢发送 body，buf_ 会持续增长直到 OOM。生产级应设上限并返回 413 Payload Too Large。

JSON 处理
extract_json_string 只支持扁平 JSON：不支持嵌套对象、数组、\uXXXX 转义；如果值里包含 \" 转义引号，提取可能提前结束。

json_escape 只转义 "、\、\n、\r、\t 和 0x00-0x1F 控制字符：不处理非 ASCII 字符（UTF-8 字节原样输出，合法）。

数据库与并发
方案 A 单线程单连接：所有请求串行处理，一个慢查询会阻塞整个事件循环。如果需要并发，升级为方案 B（每个工作线程独立 MySQLConnection）。

无自动重连：MySQL 默认 wait_timeout 8 小时，长时间空闲后第一个请求会因连接断开失败（返回 500），之后所有请求都失败，直到重启服务。生产级应在 execute 失败时检测 CR_SERVER_GONE_ERROR 并重连。

无事务：create / remove 都是单条 SQL，单条语句本身原子，不需要事务；如果将来加"创建 + 审计日志"等多语句操作，需要显式 BEGIN/COMMIT。

错误响应 Content-Type 标注不一致：错误 body 是 JSON 文本，但 Content-Type 标为 text/plain; charset=utf-8。客户端如需按 JSON 解析，应忽略 Content-Type 或服务端统一改为 application/json。

安全
密码硬编码在 main.cpp：练习用，提交前应改为环境变量读取（getenv("MYSQL_PWD")）或配置文件。

extra_headers 未做 CRLF 过滤：当前只在代码里硬编码，不来自用户输入，安全；如果将来把用户输入放进响应头，必须过滤 \r 和 \n 防止 HTTP response splitting。

escape() 不是万能药：它只保证字符串字面量内部安全，不处理 LIKE 的 %/_ 通配符、数字位置、标识符（表名/列名）。当前所有 SQL 的用户输入只有 title（走 escape）和 id（int，无注入风险），所以安全。更彻底的方案是预处理语句（mysql_stmt_*）。

其他
index.html 必须在运行时的工作目录：read_file("index.html") 是相对路径，从 build/ 目录启动时需要先把 index.html 拷过去。

无日志：accept、recv、SQL 失败都没有日志输出，排查问题只能靠 curl -v 和 MySQL 的 general log。生产级应加结构化日志。