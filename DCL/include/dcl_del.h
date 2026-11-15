#pragma once
#include <string>
#include <vector>
#include "dcl_open.h" // 引入 DCLState

// del 指令核心函数
// 纯 VFS 架构下，不再需要 script_abs_path 物理路径参数
std::string del_cmd(DCLState& state, const std::vector<std::string>& args);