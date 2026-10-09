#include <iostream>


class FileGuard{
private:
    FILE* fp;
/*
打开文件
fp = fopen("a.txt", "r");  // 以只读方式打开文件
fopen 返回一个 FILE*，若打开失败则返回 NULL。 
常见模式包括：
"r"：只读
"w"：只写（清空原内容）
"a"：追加写入
"r+"、"w+"、"a+"：读写模式
"rb"、"wb" 等：二进制文件模式 
读写文件
读取：fread、fgetc、fgets、fscanf 等
写入：fwrite、fputc、fputs、fprintf 等
这些函数都需要 FILE *fp 作为文件流对象
*/
public:
    FileGuard(const char* filename, const char* mode);
    ~FileGuard();

    FileGuard(const FileGuard&) = delete; // 禁止拷贝构造
    FileGuard& operator=(const FileGuard&) = delete; // 禁止拷贝赋值

    FILE* get() const { return fp; } // 获取文件指针
};