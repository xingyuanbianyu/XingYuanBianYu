#pragma once
#include <string>
#include <vector>
#include "dcl_open.h" // 引入 DCLState

// del 指令核心函数
// 纯 VFS 架构下，不再需要 script_abs_path 物理路径参数
// 默认版本：不允许绝对路径，限制在当前目录操作
std::string del_cmd(DCLState& state, const std::vector<std::string>& args);

// 🚀 新增重载版本：支持跨环境（允许绝对路径）删除。
// 由 open 内部调用时传入 true 触发。
std::string del_cmd(DCLState& state, const std::vector<std::string>& args, bool allow_absolute);