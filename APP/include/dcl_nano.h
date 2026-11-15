#pragma once
#include <string>
#include "dcl_open.h" // 引入 DCLState 定义

// nano 指令（模拟编辑器）
// state: 当前运行状态
// vfs_path: 目标文件的 VFS 逻辑路径（如 /C:/hello.txt），不再接收物理路径
std::string nano_cmd(DCLState& state, const std::string& vfs_path);