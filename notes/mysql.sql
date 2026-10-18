#************************************************************************************
#以下为随机场景命令使用练习#
#************************************************************************************

CREATE DATABASE testdb;
USE testdb;


#在 MySQL 中创建一个 students 表，插入 3 条数据，
#然后用 EXPLAIN 看一条 WHERE name = '张三' 的查询执行计划
create table students(
    id int primary key auto_increment,
    name varchar(20) not null,
    age int not null
);
insert into students (name, age) values ('张三', 20), ('李四', 22), ('王五', 21);
explain select * from students where name = '张三';
#SIMPLE      | students | NULL       | ALL  | NULL          | NULL | NULL    | NULL |    3 |    33.33 | Using where


#再对 students 表加索引，对比 explain 前后的 type 和 rows 列
create index idx_name on students(name);
#drop index idx_name on students;
explain select * from students where name = '张三';
#SIMPLE      | students | NULL       | ref  | idx_name      | idx_name | 82      | const |    1 |   100.00 | NULL
#type 从 all 变 ref。  rows 从 3 变 1.


#开启 MySQL 慢查询日志，写一条没有索引的查询，让 MySQL 记录它
SET GLOBAL slow_query_log = 'ON';
SET GLOBAL long_query_time = 0;
#类慢查询
SELECT name, age FROM students where age > 18;
#查看日志文件 cat /var/log/mysql/slow.log
#使用 mysqldumpslow 分析 mysqldumpslow -s t -t 10 /var/log/mysql/slow.log
/*
Query_time: SQL 语句执行耗时（秒）
Rows_sent: 查询返回给客户端的结果行数
Rows_examined: 存储引擎层为了执行这条 SQL，实际扫描了多少行数据
*/
SET GLOBAL long_query_time = 0.5;
SET GLOBAL log_queries_not_using_indexes = OFF;


#开两个 MySQL 客户端，各自 BEGIN 一个事务，
#一个 UPDATE students SET age=30 WHERE id=1  //  另一个 SELECT * FROM students WHERE id=1
/*
现象：第一个更新之后第二个查到的还是原来的

第一个事务更新：
InnoDB 此时在 id=1 这一行上加了排他锁（X 锁）。因为还没提交，A 手里死死攥着这把锁。

第二个事务查询：
快照读，通过 MVCC（多版本并发控制）机制读取的 Undo Log 里的历史版本，“读写不冲突”

事务 A：COMMIT;  底层现象：A 释放了 X 锁，且硬盘上的真实数据变成了 age=30。
事务 B：SELECT * FROM students WHERE id=1;
依然是原来的结果
结论 ：在 REPEATABLE READ 级别下，事务 B 的第一次 SELECT 已经生成了快照，
整个事务期间它都会读这个快照（可重复读），即使别人改了数据并提交了，它也装作看不见。
事务 B：COMMIT;SELECT * FROM students WHERE id=1;
是更新的结果

如果是当前读：
A 执行 UPDATE 后不提交
事务 B 查询会卡住
当前读，它必须去读磁盘上的最新数据，并加上排他锁。但 A 已经加了 X 锁，B 只能干等。

AI 提示：
锁是针对索引加的：这次的实验 WHERE id=1 命中了主键索引，所以加的是行级锁。
如果你的 WHERE 条件没走索引（比如查一个没有索引的 name），InnoDB 会退化成表锁，锁住整张表
*/
