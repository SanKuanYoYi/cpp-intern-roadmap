<!-->
此文件粘贴各 demo 的解析、说明等文档，以备需要时阅读
<-->

# mysql_conn.cpp

1. unsigned long mysql_real_escape_string(MYSQL *mysql,
                                       char *to,
                                       const char *from,
                                       unsigned long length);
参数	你的传参	作用
mysql	conn_	必须，因为它需要知道当前连接的字符集（character_set_connection），转义规则依赖字符集
to	buf.data()	输出缓冲区，写入转义后的字节
from	s.c_str()	输入字节
length	s.size()	显式长度，不是 strlen
返回	len	写入 to 的字节数（不含结尾 \0）

关键点：传 s.size() 而不是依赖 strlen


为什么缓冲区是 2 * s.size() + 1
官方文档的硬性要求：to 缓冲区至少 2*length + 1 字节 (最坏情况下每个输入字节都扩展成两个字节 + '\0')


if (!conn_) return s; 防御
mysql_real_escape_string(nullptr, ...) 是未定义行为
后续 execute 也会因为 !conn_ 直接失败，不会真的把未转义字符串发出去


为什么 const 成员函数能调用非 const 的 mysql_real_escape_string ?
conn_ 是 MYSQL* const：不能写 conn_ = ...，但 *conn_ 可以被修改
mysql_real_escape_string 参数是 MYSQL*（非 const）：接受 MYSQL* const 没问题

std::string(buf.data(), len) 为什么用长度构造 ?
std::string(buf.data())（单参数，读到 \0 停）
明确表达意图：我们已知长度 len，直接用长度构造，语义清晰，也不依赖 mysql_real_escape_string 是否保证写 \0

一个容易忽略的陷阱：字符集
如果连接字符集和实际数据字符集不一致，转义结果可能被服务器按另一种字符集解释，导致转义失效
正确做法：连接建立后立刻执行  SET NAMES utf8mb4;

% 和 _：escape 管不了 LIKE 等等其他一些场景
终极方案是预处理语句......


# student_manager.cpp

1. std::string escaped_name = conn_.escape(name);
为什么只转义 name，不转义 age / score：
age 是 int，score 是 double，经过 std::cin >> 读入，类型系统已经保证了它们只能是数值
规则：escape 只用于将要被单引号包裹的字符串值


2. 用 std::ostringstream 拼 SQL
为什么用 ostringstream 而不是 std::to_string 
std::to_string 对 double 默认只保留 6 位小数
ostringstream 默认用 %g 风格但精度由 precision() 控制
默认 precision(6)，对 double 是6 位有效数字，不是 6 位小数。
如果分数精度要求高，应该 oss << std::setprecision(15);   // 需要 #include <iomanip>

conn_.execute(oss.str()) 的返回值
execute 内部是 mysql_query(conn_, sql.c_str()) == 0：
返回 true：mysql_query 返回 0，语句执行成功。
返回 false：非 0，失败。

mysql_insert_id(conn_.raw())：取自增 ID
conn_.raw() 返回内部 MYSQL*
返回类型 uint64_t，是上一条 INSERT 为 AUTO_INCREMENT 列生成的值
mysql_insert_id 必须紧跟在成功的 INSERT 之后调用。
如果中间又执行了别的 INSERT/UPDATE，或者连接被重置，返回值就变了


3. ResultSet rs(conn_.query(sql));
rs 是 ResultSet 对象，不是指针。!rs 能编译，是因为 ResultSet 定义了：
explicit operator bool() const { return res_ != nullptr; }
所以 !rs 等价于 rs.res_ == nullptr，即"结果集无效"

不需要 mysql_free_result：rs 内部 res_ 是 nullptr，~ResultSet 里的 if (res_) 判断为假，不会调 mysql_free_result(nullptr)。
不需要 mysql_close：conn_ 是 MySQLConnection 成员，函数返回后对象继续存活，连接不关闭。
return 直接退出 search_student，rs 析构，一切干净。

MySQLConnection::query 的契约是"执行 SELECT"，调用方传 SELECT，所以 nullptr 一律按错误处理是合理的。
如果以后把 query 用于非 SELECT，需要改成用 mysql_field_count 或 mysql_errno 区分。

if (!rs) { /* SQL 层面失败 */ }
// 到这里 rs 有效，但可能是 0 行
MYSQL_ROW row = mysql_fetch_row(rs.get());
if (!row) { /* 查询成功，但没匹配到行 */ }


# cmake

1. cmake_minimum_required(VERSION 3.10)
声明本项目最低要求的 CMake 版本。低于 3.10 的 CMake 会直接报错退出


2. project(student_manager_mysql CXX)
定义项目名 student_manager_mysql，并声明只使用 CXX（C++） 语言。
CXX 告诉 CMake 不要去找 C 编译器，只配置 C++ 工具链。
可选的还有 C、CUDA 等；不写语言参数则默认启用 C 和 CXX


3. set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
第一行：让所有目标默认用 C++17 标准编译（等价于给编译器传 -std=c++17）。
第二行：ON 表示"如果编译器不支持 C++17，就报错"，而不是悄悄降级到 C++14/11。
没有这行的话，老编译器可能静默用低标准编译，导致 std::exchange、if constexpr 之类特性编译失败


4. find_package(PkgConfig REQUIRED)
让 CMake 去找系统上的 pkg-config 工具（一个命令行程序，不是库）。
REQUIRED 表示找不到就报错停止，而不是继续往下走然后链接时莫名其妙失败。

pkg-config 是 Unix 生态里"库的元数据查询工具"。
每个库安装时会附带一个 .pc 文件（如 mysqlclient.pc），里面记录了：
头文件在哪（Cflags: -I/usr/include/mysql）
库文件在哪（Libs: -L/usr/lib/x86_64-linux-gnu -lmysqlclient）
版本号、依赖的其他库
//pkg-config --cflags mysqlclient   # 输出 -I/usr/include/mysql
//pkg-config --libs mysqlclient     # 输出 -L... -lmysqlclient


pkg_check_modules(MYSQL REQUIRED mysqlclient)
这是 PkgConfig 提供的 CMake 函数，作用是：调用 pkg-config 查询 mysqlclient 这个包，把结果存进一组以 MYSQL 为前缀的 CMake 变量里。
MYSQL 是你自己起的变量前缀名，只要后面引用时一致。
mysqlclient 是 .pc 文件名（去掉 .pc 后缀），必须是系统上实际存在的名字。


5. add_executable(student_manager
    main.cpp
    student_manager.cpp
    mysql_conn.cpp
)
定义一个名为 student_manager 的可执行目标，源文件是后面列出的三个 .cpp。
CMake 会为它生成编译规则，最终产物就是 student_manager


6. 三个 target_* 命令：把 MySQL 信息挂到目标上
target_include_directories(student_manager PRIVATE ${MYSQL_INCLUDE_DIRS})
给 student_manager 的编译阶段加上头文件搜索路径。
等价于给编译器传 -I/usr/include/mysql

target_link_directories(student_manager PRIVATE ${MYSQL_LIBRARY_DIRS})
给链接阶段加上库文件搜索路径，等价于 -L/usr/lib/x86_64-linux-gnu。

target_link_libraries(student_manager PRIVATE ${MYSQL_LIBRARIES})
告诉链接器"把 mysqlclient 这个库链进来"，等价于 -lmysqlclient。

为什么去掉 Save / Load From File
数据已经在 MySQL 里，std::vector 与文件不再是持久化来源，
students_ 成员和 save_in_file / load_from_file 全部删除，菜单项 7、8 换成"清空所有学生"和"退出"。