/*
复习自用
*/

/**************************************************************************/

#include <cstring>

class MyString {
private:
  char *data_;
  unsigned int len_;

public:
  MyString(const char *newData);

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
    delete[] data_;
    len_ = other.len_;
    data_ = new char[len_ + 1];
    std::strcpy(data_, other.data_);
  }
  return *this;
}

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
  void push(T &&value) {
    {
      std::lock_guard<std::mutex> lk(mtx_);
      q_.push(std::forward(value));
    }
    cv_.notify_one();
  }
  /*漏掉了mutable：让 const 成员函数也能加锁*/
  /*有点分不清lock_guard和unique_lock区别*/
  /*入队错写成了引用传递，会导致无法传入右值临时对象、move*/
  /*应该用值传递+move 或者 万能引用&&+forward*/

  bool try_pop(T &value) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!q_.empty())
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