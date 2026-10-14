/*
自测练习使用。无其他用途
*/


#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

class FileGuardBroken {
private:
    FILE *fp_;

public:
    FileGuardBroken(const char *filename, const char *mode)
        : fp_(std::fopen(filename, mode)) {
        if (!fp_) {
            throw std::runtime_error(std::string("fopen failed: ") + filename);
        }
    }

    ~FileGuardBroken() {
        if (fp_) {
            std::fclose(fp_);
        }
    }

    // 故意不写：
    // FileGuardBroken(const FileGuardBroken&) = delete;
    // FileGuardBroken& operator=(const FileGuardBroken&) = delete;

    FILE *get() const { return fp_; }
};



int main() {
    FileGuardBroken f1("double_free_test.txt", "w");
    std::fputs("first\n", f1.get());

    {
        FileGuardBroken f2 = f1; // 默认拷贝构造：f2.fp_ == f1.fp_
        std::fputs("second\n", f2.get());
        std::cout << "f2 即将析构，会 fclose 同一个 FILE*\n";
    } // f2 析构，关闭 FILE*

    std::cout << "内层作用域结束,f1.fp_ 仍非空,f1 析构时会再次 fclose\n";
    return 0;
}
