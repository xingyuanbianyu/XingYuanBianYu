#pragma once
#include <string>

// 操作码结构体
struct OpCode {
    std::string binary; // 二进制操作码，如 "1100100110010"
    int line;           // 行号
};