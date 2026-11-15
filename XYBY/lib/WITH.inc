// ============================================================
// lib/WITH.inc —— WITH 自动文件资源管理（名称精准对齐版 + 变量回退）
// 完全适配 executor.cpp 中的调用：write_data, on_brace_close, with_read_all
// ============================================================
#pragma once
#include <fstream>
#include <vector>
#include <memory>
#include <string>
#include <iostream>
#include <sstream> // with_read_all 用到了 stringstream，必须包含

// 使用 inline 函数内的 static 变量，彻底避免 duplicate symbol 报错
inline std::vector<std::shared_ptr<std::fstream>>& get_with_stack() {
    static std::vector<std::shared_ptr<std::fstream>> stack;
    return stack;
}

inline std::fstream* get_current_with_stream() {
    auto& stack = get_with_stack();
    if (stack.empty()) return nullptr;
    return stack.back().get();
}

// WITH 语句处理器
inline bool handleWithOpen(const std::string& line, MemorySpace& mem) {
    if (line.find("KID:WITH_OPEN") == std::string::npos) return false;

    std::string mode = "r";
    size_t mode_pos = line.find("MODE:");
    if (mode_pos != std::string::npos) {
        size_t s = mode_pos + 5;
        if (line.compare(s, 4, "VID:") == 0) {
            size_t var_s = s + 4, var_e = var_s;
            while (var_e < line.size() && (isalnum((unsigned char)line[var_e]) || line[var_e] == '_')) var_e++;
            std::string var_name = line.substr(var_s, var_e - var_s);
            
            // 【核心修改】尝试从内存读取变量值
            mode = resolve_deep_value(var_name, mem);
            
            // 如果变量没有值（None/空），直接把变量名当作模式字符串！
            if (mode == "None" || mode.empty()) {
                mode = var_name; 
                std::cerr << "[WITH] 变量 [" << var_name << "] 未定义，回退使用名称作为模式。" << std::endl;
            }
        } else {
            size_t e = s;
            while (e < line.size() && (isalnum((unsigned char)line[e]) || line[e] == '_')) e++;
            mode = line.substr(s, e - s);
        }
    }

    std::string path = "";
    size_t path_pos = line.find("PATH:SID:22[");
    if (path_pos != std::string::npos) {
        size_t s = path_pos + 12;
        size_t e = line.find("]22", s);
        path = line.substr(s, e - s);
    }

    if (path.empty()) {
        std::cerr << "[WITH] 错误: 无法解析文件路径" << std::endl;
        return true;
    }

    std::ios::openmode om = std::ios::in;
    if (mode == "w")      om = std::ios::out;
    else if (mode == "a") om = std::ios::app;
    else if (mode == "r+") om = std::ios::in | std::ios::out;
    else if (mode == "w+") om = std::ios::in | std::ios::out | std::ios::trunc;
    else if (mode == "a+") om = std::ios::in | std::ios::out | std::ios::app;
    else if (mode == "rb") om = std::ios::in | std::ios::binary;
    else if (mode == "wb") om = std::ios::out | std::ios::binary;
    else if (mode == "ab") om = std::ios::app | std::ios::binary;

    auto stream = std::make_shared<std::fstream>(path, om);
    if (!stream->is_open()) {
        std::cerr << "[WITH] 错误: 无法打开文件 " << path << " (模式:" << mode << ")" << std::endl;
        return true;
    }

    get_with_stack().push_back(stream);
    std::cerr << "[WITH] 成功接管文件: " << path << " (模式:" << mode << ")" << std::endl;
    return true;
}

// 【第 1205 行调用】精准对齐：on_brace_close
inline void on_brace_close(int brace_level) {
    auto& stack = get_with_stack();
    if (!stack.empty()) {
        stack.pop_back();
        std::cerr << "[WITH] 作用域结束，自动关闭文件" << std::endl;
    }
}

// 【第 810 行调用】精准对齐：with_read_all
inline std::string with_read_all() {
    std::fstream* fs = get_current_with_stream();
    if (!fs) return "Error: No active WITH file";
    std::stringstream buf; 
    buf << fs->rdbuf();
    return buf.str();
}

// 【第 818 行调用】精准对齐：write_data
inline bool write_data(const std::string& content) {
    std::fstream* fs = get_current_with_stream();
    if (!fs) {
        std::cerr << "[WITH] 错误: 不在 WITH 块内，无法写入！" << std::endl;
        return false;
    }
    (*fs) << content;
    fs->flush();
    return true;
}