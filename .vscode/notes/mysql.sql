#************************************************************************************
#以下为随机场景命令使用练习#
#************************************************************************************

#在 MySQL 中创建一个 students 表，插入 3 条数据，
#然后用 EXPLAIN 看一条 WHERE name = '张三' 的查询执行计划
create table students(
    id int primary key auto_increment,
    name varchar(20) not null,
    age int not null
);
insert into students (name, age) values ('张三', 20), ('李四', 22), ('王五', 21);
explain select * from students where name = '张三';

