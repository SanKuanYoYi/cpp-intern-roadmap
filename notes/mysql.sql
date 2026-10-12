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