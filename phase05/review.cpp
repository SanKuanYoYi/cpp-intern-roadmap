/*
复习自用
*/

/**************************************************************************/

#include <cstring>
#include <algorithm>

class MyString {
private:
  char *data_;
  unsigned int len_;

public:
  MyString(const char *newData = "");

  MyString(const MyString &other);
  MyString &operator=(const MyString &other);

  MyString(MyString &&other) noexcept;
  MyString &operator=(MyString &&other) noexcept;

  ~MyString();

  const char *getString() const { return data_; }
  const unsigned int grtLen() const { return len_; }
};

MyString::MyString(const char *newData) {
  len_ = strlen(newData);
  data_ = new char[len_ + 1];
  std::strcpy(data_, newData);
}

MyString::~MyString() {
  len_ = 0;
  delete[] data_;
}

MyString::MyString(const MyString &other) {
  len_ = other.len_;
  data_ = new char[len_ + 1];
  std::strcpy(data_, other.data_);
}

MyString &MyString::operator=(const MyString &other) {
  if (this != &other) {
    MyString temp(other);
    std::swap(data_, temp.data_);
    std::swap(len_, temp.len_);
  }
  return *this;
}
/*仍错采用了delete+new+strcpy的写法————可能悬空指针等问题*/

MyString::MyString(MyString &&other) noexcept {
  len_ = other.len_;
  data_ = other.data_;
  other.len_ = 0;
  other.data_ = nullptr;
}

MyString &MyString::operator=(MyString &&other) noexcept {
  if (this != &other) {
    delete[] data_;
    data_ = other.data_;
    len_ = other.len_;
    other.data_ = nullptr;
    other.len_ = 0;
  }
  return *this;
}

/*  错误版本  */
// MyString &MyString::operator=(MyString &&other) noexcept {
//   if (this != &other) {
//     delete[] data_;
//     len_ = other.len_;
//     data_ = new char[len_ + 1];
//     std::strcpy(data_, other.data_);
//     other.len_ = 0;
//     other.data_ = nullptr;
//   }
//   return *this;
// }
/*
刚开始把移动写成了在做深拷贝，而不是“偷”资源
正确的移动赋值应该直接接管指针，然后把对方的指针置为 nullptr
现代 C++ 开发中最推荐的移动赋值写法是两个swap
*/

/**************************************************************************/

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>

template <typename T> class ThreadSafeQueue {
private:
  std::queue<T> q_;
  mutable std::mutex mtx_;
  std::condition_variable cv_;

public:
  void push(T value) {
    {
      std::lock_guard<std::mutex> lk(mtx_);
      q_.push(std::move(value));
    }
    cv_.notify_one();
  }
  /*漏掉了mutable：让 const 成员函数也能加锁*/
  /*有点分不清lock_guard和unique_lock区别*/
  /*入队错写成了引用传递，会导致无法传入右值临时对象、move*/
  /*应该用值传递+move 或者 万能引用&&+forward*/

  bool try_pop(T &value) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (q_.empty())
      return false;
    value = std::move(q_.front());
    q_.pop();
    return true;
  }
  /*错没写为move，把要pop的对象又深拷贝了一份*/

  void wait_and_pop(T &value) {
    std::unique_lock<std::mutex> lk(mtx_);
    cv_.wait(lk, [this] { return !q_.empty(); });
    value = std::move(q_.front());
    q_.pop();
  }
  /*错写为不带参的，有风险。搞混、忘记了参数的作用*/
  /*记不清有无谓词的wait意义*/
  /*忽略了用unique_lock的必要性*/

  bool empty() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return q_.empty();
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return q_.size();
  }
  /*分不清各种整型变量的使用*/
};

/**************************************************************************/

#include <sstream>
#include <string>

std::string make_http_response(int code, const std::string &status,
                               const std::string &content_type,
                               const std::string &body) {
  std::ostringstream ss;
  ss << "HTTP/1.1 " << code << " " << status << "\r\n";
  ss << "Content-Type: " << content_type << "\r\n";
  ss << "Content-Length: " << body.size() << "\r\n";
  ss << "Connection: close\r\n";
  ss << "\r\n";
  ss << body;
  return ss.str();
}
/*记不清格式顺序*/
/*漏掉空格*/
/*拼写错误*/

#include <iostream>

int main() {
  // MyString: 拷贝、移动、赋值
  MyString s1("hello");
  MyString s2 = s1;            // 拷贝
  MyString s3 = std::move(s1); // 移动
  s2 = s3;                     // 拷贝赋值
  s2 = std::move(s3);          // 移动赋值
  std::cout << "MyString ok\n";

  // ThreadSafeQueue: 单线程 push/pop
  ThreadSafeQueue<int> q;
  q.push(1);
  q.push(2);
  int v;
  if (!q.try_pop(v) || v != 1) {
    std::cout << "FAIL try_pop\n";
    return 1;
  }
  if (!q.try_pop(v) || v != 2) {
    std::cout << "FAIL try_pop\n";
    return 1;
  }
  if (q.try_pop(v)) {
    std::cout << "FAIL empty pop\n";
    return 1;
  }
  std::cout << "TSQ ok\n";

  // make_http_response
  std::string resp = make_http_response(200, "OK", "text/plain", "hi");
  if (resp.find("Content-Length: \r\n") == std::string::npos) {
    std::cout << "FAIL http response\n";
    return 1;
  }
  std::cout << "HTTP ok\n";

  return 0;
}

// valgrind --leak-check=full ./review
/*
==29668== Memcheck, a memory error detector
==29668== Copyright (C) 2002-2024, and GNU GPL'd, by Julian Seward et al.
==29668== Using Valgrind-3.26.0 and LibVEX; rerun with -h for copyright info
==29668== Command: ./review
==29668==
MyString ok
TSQ ok
HTTP ok
==29668==
==29668== HEAP SUMMARY:
==29668==     in use at exit: 0 bytes in 0 blocks
==29668==   total heap usage: 9 allocs, 9 frees, 75,945 bytes allocated
==29668==
==29668== All heap blocks were freed -- no leaks are possible
==29668==
==29668== For lists of detected and suppressed errors, rerun with: -s
==29668== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)

*/