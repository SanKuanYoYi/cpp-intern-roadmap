<!-->
此文件粘贴各 demo 的解析、说明等文档，以备需要时阅读
<-->

# mysql_basic.cpp

1. MYSQL* conn = mysql_init(nullptr)
官方声明（mysql.h）：MYSQL *mysql_init(MYSQL *mysql);
它接收一个 MYSQL *，返回一个 MYSQL *。

参数传 nullptr 是什么意思
mysql_init 的参数语义是："请在这块内存上初始化一个 MYSQL 对象"。
传 nullptr 表示："我自己没有现成的 MYSQL 对象，你（库）帮我 malloc 一块并初始化好"

写法	语义
mysql_init(nullptr)	库自己分配一个 MYSQL 结构，返回其指针。
                    调用者之后要用 mysql_close() 释放，不需要自己 free。
MYSQL obj; mysql_init(&obj);	调用者自己提供内存（栈上或静态区），库只初始化不分配。
                                这种情况下不能对该指针调用 mysql_close()，因为它不是 malloc 出来的。

mysql_init 具体做了什么
分配内存：内部 calloc 一块 MYSQL 结构
初始化内部字段
初始化客户端库

成功：返回指向新 MYSQL 对象的非空指针。
失败：返回 NULL

规则：只要 mysql_init 成功过、conn 非空，之后任何失败分支在 return 前都要 mysql_close(conn)，
否则 mysql_init 分配的那块 MYSQL 结构就泄漏了。


2. mysql_real_connect
官方原型是:
MYSQL *mysql_real_connect(
    MYSQL *mysql,               // 1. 已初始化的连接句柄
    const char *host,           // 2. 主机名或 IP
    const char *user,           // 3. MySQL 用户名
    const char *passwd,         // 4. 密码
    const char *db,             // 5. 默认数据库名
    unsigned int port,          // 6. 端口号
    const char *unix_socket,    // 7. Unix socket 路径
    unsigned long client_flag   // 8. 客户端标志位
);

port	0	0 表示使用默认端口（通常是 3306）
unix_socket	nullptr	nullptr 表示使用默认 socket 路径
client_flag	0	不启用任何特殊客户端标志（如多语句、压缩等）

成功：返回一个 MYSQL * 连接句柄，其值与第一个参数 mysql 相同。
失败：返回 NULL。

失败分支必须调用 mysql_close(conn)
关闭连接（如果已建立），并释放由 mysql_init 分配的连接句柄内存。
如果连接失败后不调用 mysql_close，mysql_init 分配的那块 MYSQL 结构就会永久泄漏

mysql_error(conn) 的作用
mysql_real_connect 失败后，错误信息会被记录在这个句柄上，mysql_error 可以从中取出人类可读的错误原因


3. int mysql_query(MYSQL *mysql, const char *stmt_str);
stmt_str：以 '\0' 结尾的 SQL 语句字符串。
长度由 strlen 决定（所以不能含 \0；要含二进制数据得用 mysql_real_query + 显式长度）

返回值	含义
0	成功：语句被服务器接收并执行完毕
非 0	失败：语句有语法错误、权限不足、表不存在等

"成功" ≠ "有结果集"
INSERT/UPDATE/DELETE ；SELECT 成功但匹配 0 行 ; 等等

mysql_query 一次只能发一条语句

