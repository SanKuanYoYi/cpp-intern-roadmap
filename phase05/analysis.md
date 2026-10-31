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



4. MYSQL_RES *mysql_store_result(MYSQL *mysql);
把整个结果集一次性从服务器拉回客户端内存，放进 MYSQL_RES
返回：指向 MYSQL_RES 的指针；失败或"无结果集"时返回 NULL。

MYSQL_RES 是一个不透明结构体.内部持有：
列数、每列的元数据（列名、类型、长度等）
所有行的数据（mysql_store_result 的"store"就是存这里）
当前读取位置的游标

之后通过 mysql_num_fields(result)、mysql_num_rows(result)、mysql_fetch_row(result) 
等函数操作它，全部通过函数访问

返回 NULL 的两种情况（核心）
情况 A：真的出错: mysql_error(conn) 有描述。
情况 B：这条语句本来就不产生结果集: mysql_errno(conn) 是 0
方式一：mysql_field_count(conn)
返回最近一次查询结果的列数：SELECT 返回 ≥1，非 SELECT 返回 0
方式二：mysql_errno(conn)
demo 因为是 select 只检查 errno 简化，实际上需要都检查


mysql_store_result 和 mysql_use_result 区别
                mysql_store_result()	mysql_use_result()
数据位置	一次性全部读入客户端内存	不读入客户端，逐行从服务器拉取
内存占用	占用与结果集大小成正比的内存	只占当前行和一个通信缓冲区
速度	一次性接收，之后本地遍历很快	启动快，但每行都要和服务器通信
对服务器影响	读完即释放服务器资源	在逐行读取期间会占用服务器，阻止其他线程更新相关表
使用限制	无特殊限制	                必须一直 mysql_fetch_row() 到 NULL，
                                    否则下一查询会报 Commands out of sync；
                                    不能使用 mysql_data_seek() 等定位函数
简单记：数据量小、需要反复遍历结果 → mysql_store_result()；
数据量大、希望客户端内存占用低 → mysql_use_result()，但要尽快读完


5. MYSQL_ROW mysql_fetch_row(MYSQL_RES *result);
typedef char **MYSQL_ROW;
MYSQL_ROW row; 声明的是一个 char **——指向"字符串指针数组"的指针。
每一行数据被表示成一个以 NULL 结尾的 char* 数组，row[i] 是第 i 列值的 C 字符串

返回：
还有下一行 → 返回该行的 MYSQL_ROW（内部指针，指向结果集内部缓冲区）。
读完了 → 返回 NULL。
出错（读游标推进失败）→ 也返回 NULL，此时 mysql_errno(conn) 非 0

row[0]  // 类型 char*，值是 "1"
row[1]  // char*，值是 "Alice"
row[2]  // char*，值是 "18"
row[3]  // char*，值是 "90"（或 "90.0"，取决于服务器的 DOUBLE 格式化）
要用数值就必须自己转

如果某列在数据库里是 NULL，row[i] 会是 NULL 指针，不是字符串 "NULL"
通用代码要先判：if (row[i] == nullptr) { /* 处理 NULL */ }

mysql_free_result(result);  // 先释放结果集
mysql_close(conn);          // 再关闭连接

mysql_free_result(result)：释放由 mysql_store_result() 或 mysql_use_result() 返回的结果集对象所占用的客户端内存
mysql_close(conn)：关闭与 MySQL 服务器的连接，并释放 MYSQL * 连接句柄相关的资源
在长连接场景下，每次查询后都应该 mysql_free_result；最终退出时再 mysql_close

MYSQL_ROW row 的生命周期
row 指向的是 result 内部缓冲区，不是 mysql_fetch_row 新分配的内存
result 被 mysql_free_result 释放后，所有 row 指针全部失效
所以如果需要保留某行数据，必须拷贝，不能只保存指针


mysql_real_escape_string 是干什么的？为什么 SELECT 不必须，但 INSERT 必须？
mysql_real_escape_string() 把一段原始字符串转义成可以安全嵌入 SQL 语句字面量中的形式
它会在 \0、\n、\r、\t、\、单引号、双引号等字符前加上反斜杠，并考虑当前连接的字符集，确保转义后的字符串不会“逃出”单引号/双引号的范围

为什么 SELECT 不必须
SELECT 是硬编码的固定字符串，没有任何用户输入被拼进去，不存在“用户输入破坏 SQL 结构”的可能
为什么 INSERT 必须：INSERT 通常会把外部数据（用户输入、文件内容、网络请求等）拼接到 SQL 中
否则可能会执行错误或者 SQL 注入
所以原则是：任何来自外部的字符串要拼进 SQL 字面量，都应该先 mysql_real_escape_string


# mysql_conn.cpp

1. mysql_real_connect 失败后 close 后：conn_ = nullptr
不是必需的，但有工程价值
理由 A：和移动操作保持同一纪律
理由 B：防止未来重构引入 bug
理由 C：调试时不会看到"已释放的指针"

更干净的写法：抽一个 reset() 私有
private:
    void reset() noexcept {
        if (conn_) {
            mysql_close(conn_);
            conn_ = nullptr;
        }
    }
需要释放时直接 reset()

析构函数不存在这种重构可能：它已经是生命周期的终点，
没有任何"改写成不抛异常 / 让对象存活"的合理解释空间。
析构就是最后一步，之后一定是对象消失
所以可以不置空


2. std::exchange(other.conn_, nullptr)
读出 other.conn_ 的当前值。把 other.conn_ 写成 nullptr。返回第 1 步读到的旧值

: conn_(std::exchange(other.conn_, nullptr)) {}
整行等价于：
: conn_(other.conn_)        // 初始化：新对象接管源对象的句柄
{
    other.conn_ = nullptr;      // 函数体：源对象置空
}

两段代码语义完全一致。用 std::exchange 的版本有两个好处：
不会写反顺序。一行完成，适合初始化列表。

noexcept 的作用
MySQLConnection 禁止拷贝（= delete），所以如果移动构造不是 noexcept，
std::vector<MySQLConnection> 在扩容时既不能拷贝也不能安全移动。
加 noexcept 后，容器放心用移动

为什么"源对象置空"是必须的
置空后，a 析构时 conn_ == nullptr，~MySQLConnection 里的 if (conn_) 直接跳过，安全


3. execute / query 
为什么用 std::string 参数、为什么内部要 .c_str()

如果用 const char*，调用方会面对一堆麻烦：
场景	    const char* 参数	                std::string 参数
传字面量	conn.execute("SELECT ...") 可以	    隐式构造 std::string，可以
传已有      std::string	必须 .c_str()	        直接传
拼接 SQL	得手动 char buf[...]            	std::string sql = "INSERT ... " + value; 自动扩容
生命周期	调用方得保证指针在函数返回前一直有效	值语义，拷贝/移动自动管理
异常安全	手动 malloc/free 易泄漏	            自动析构

int mysql_query(MYSQL *mysql, const char *stmt_str);
它只接受 C 字符串（以 '\0' 结尾的 char*）。
std::string 是 C++ 类型，标准不保证它的内部缓冲区在末尾有 '\0'
（C++11 起 c_str() 返回的才是保证以 '\0' 结尾的，且 data() 在 C++17 起也保证，
但直接传 std::string 给 C 函数本来就不合法，编译不过）

c_str() 返回 const char*，指向 sql 内部缓冲区，生命周期与 sql 相同
C++17 起 std::string::data() 也返回 const char* 且保证 '\0' 结尾，所以 sql.data() 也能编译通过


4. 能不能让 execute 直接调用 query
不能直接调用，因为两者语义不同，而且直接复用会同时踩"误判失败"和"内存泄漏"两个坑。

        execute	                                                    query
用途	执行不产生结果集的语句（INSERT / UPDATE / DELETE / DDL）	执行 SELECT，产生结果集
是否 mysql_store_result	不调用	                                必须调用，否则结果集挂在连接上
返回	bool（成功/失败）	                                MYSQL_RES*（调用方负责 mysql_free_result）
成功判据	mysql_query 返回 0	                    mysql_query 返回 0 且 mysql_store_result 非空

推荐做法：抽一个私有 send_query 共享
private:
    bool send_query(const std::string& sql) {
        if (!conn_) return false;
        return mysql_query(conn_, sql.c_str()) == 0;
    }


5. mysql_errno vs mysql_error
这两个函数都作用于最近一次可能失败或成功的 MySQL API 调用，但返回的信息形式不同。

mysql_errno(MYSQL *mysql)	mysql_error(MYSQL *mysql)
返回值	unsigned int，错误编号	const char*，以 '\0' 结尾的错误文本
成功时	返回 0	返回空字符串 ""
用途	程序逻辑判断：用 if 或 switch 精确匹配特定错误（如 CR_CONN_HOST_ERROR）	人类阅读：直接打印或记录到日志，用于诊断问题
示例	2013	"Lost connection to MySQL server during query"

6. 为什么每个成员函数开头都要检查 conn_
核心原因：conn_ == nullptr 是这个类唯一能表示的“无效/已移动”状态
防御性编程：MYSQL* 不是“空指针安全”的
移动后对象的状态：conn_ 变为 nullptr 是设计的一部分
接口契约：让“无效对象”以明确方式失败

7. 为什么不 free_result
MySQLConnection 内部看不到 mysql_free_result，是因为结果集的所有权被 query() 的返回值完整转移给了调用方——
结果集的生命周期比 query() 函数本身长，不可能在函数内部释放

资源	谁分配	谁释放	归属
MYSQL* 连接句柄	构造函数 mysql_init	析构函数 mysql_close	MySQLConnection
MYSQL_RES* 结果集	query() 内 mysql_store_result	调用方 mysql_free_result	调用方
MYSQL_ROW 行指针	结果集内部	随结果集释放	不独立归属

谁调用 store_result，谁负责 free_resul

为什么不在析构函数里"兜底"释放
mysql_close() 会关闭连接，但不会释放调用方已经通过 mysql_store_result 取走的 MYSQL_RES*——后者是独立分配的内存块

代价：调用方必须自己管结果集    
想彻底 RAII：再封装一个 MySQLResult

