#pragma once
#include <string>
#include <vector>
#include <functional>   // 🚀 新增：为了 std::function

struct DCLState {
    std::string cwd;
    std::string path;
    std::string data;
    std::string cmd;
};

// 🚀 vector 版本（带 xyn_executor 回调，用于把 .xyn 转交给 cli.inc 执行）
std::string open_cmd(DCLState& state, const std::vector<std::string>& raw_args,
                     std::function<void(const std::string&)> xyn_executor = nullptr);

// 🚀 3 参数版本（兼容旧调用）
std::string open_cmd(DCLState& state, const std::string& raw_path, const std::string& raw_mode = "auto");