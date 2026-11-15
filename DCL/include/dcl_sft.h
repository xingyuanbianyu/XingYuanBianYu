#pragma once
#include <string>
#include <vector>
#include "dcl_open.h" // 引入 DCLState

// sft 指令核心函数
std::string sft_cmd(DCLState& state, const std::vector<std::string>& args);