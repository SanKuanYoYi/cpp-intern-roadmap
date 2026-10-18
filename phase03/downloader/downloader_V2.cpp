#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <string>
#include <stdexcept>
#include <iomanip>
#include <thread>
#include <exception>

// ---------- 获取文件大小（沿用 V1 ） ----------
std::uintmax_t file_size(const std::string &path)
{
    std::ifstream open(path, std::ios::binary | std::ios::ate);
    if (!open)
        throw std::runtime_error("无法打开文件、获取大小：" + path);
    const auto pos = open.tellg();
    if (pos < 0)
        throw std::runtime_error("tellg 失败：" + path);
    return static_cast<std::uintmax_t>(pos);
}


