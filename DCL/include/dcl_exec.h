#pragma once
#include <string>
#include <vector>
#include <functional>
#include "dcl_open.h" // 引入 DCLState

// ==========================================================
// 🚀 exec 执行核心函数
// ==========================================================
// state:         当前运行状态
// cmd_args:      VFS 内部逻辑路径 + 后面的参数列表（比如 ["/test.xyn", "参数1", "参数2"]）
// xyn_executor:  回调函数，用于把 .xyn 里的命令抛回给 CLI 执行
// ==========================================================
std::string exec_cmd(DCLState& state, const std::vector<std::string>& cmd_args,
                     std::function<void(const std::string&)> xyn_executor);