┌─────────────────────────────────────┐
│  main.cpp（事件循环 + 路由）         │
│    epoll_wait → recv → feed         │
│    → handle_request → to_write      │
│    → send_all → close               │
└──────────────┬──────────────────────┘
               │ 调用
               ▼
┌─────────────────────────────────────┐
│  HttpParser（字节 → HttpRequest）    │
│  HttpResponse（HttpResponse → 字节） │
└─────────────────────────────────────┘
               │ 调用
               ▼
┌─────────────────────────────────────┐
│  TodoRepo（数据访问层）              │
│  list / get / create / remove       │
└──────────────┬──────────────────────┘
               │ 调用
               ▼
┌─────────────────────────────────────┐
│  MySQLConnection（RAII + escape）    │
└──────────────┬──────────────────────┘
               │ mysql_query
               ▼
┌─────────────────────────────────────┐
│  MySQL C API（libmysqlclient）       │
└─────────────────────────────────────┘







# 一个 curl -X POST /todos 的请求，从字节到达服务器到响应发回去，经过了哪几个函数？

epoll_wait 返回事件 ->  read  ->  HttpParser::feed 返回true  -> HttpParser::request 返回&
-> handle_request 处理 extract_json_string 、执行 TodoRepo sql 、构造 HttpResponse
-> to_write 序列化response  -> send_all 发送response  ->  结束处理



# HttpParser 为什么要用 feed(data, len) 而不是 parse(buffer)
parser 每次 read 可能不完整，下次 parser 解析就失败
feed 存 buf 如果不完整可以存着下次再解析



# TodoRepo 为什么不直接返回 MYSQL_RES*，而是返回 std::vector<Todo>？
MYSQL_RES* 说明函数持有返回的 row* 
需要自己手动 mysql_free_result
std::vector<Todo> 可以直接存取多个 res 且可以自动析构 (RAII)