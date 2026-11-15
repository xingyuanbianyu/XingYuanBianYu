#pragma once
#include <string>
#include <vector>
#include "dcl_open.h" // 引入 DCLState

// ioq 指令核心函数
// 支持三种模式:
// 1. 无参数 (args为空): 重启自身
// 2. 参数为 "-q": 重启系统 (需要 root)
// 3. 参数为 pid或名字, 支持前置 "-9": 重启指定进程
std::string ioq_cmd(DCLState& state, const std::vector<std::string>& args);