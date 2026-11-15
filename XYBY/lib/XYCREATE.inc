// ============================================================
// lib/XYCREATE.inc —— XYCREATE 处理器（调用全局 xyfile_write）
// 依赖：XYFILE.inc 已在 main.cpp 全局展开
//       g_user_cwd（main.cpp 全局变量）
// ============================================================
#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <cctype>

bool handleXYCreate(const std::string& line, MemorySpace& mem) {
    if (line.find("FID:XYCREATE") == std::string::npos) return false;

    // ===== 提取参数 =====
    std::vector<std::string> args;
    size_t p = 0;
    while (p < line.size()) {
        size_t vs = line.find("VID:", p);
        size_t ss = line.find("SID:22[", p);
        if (vs == std::string::npos && ss == std::string::npos) break;

        if (ss != std::string::npos && (vs == std::string::npos || ss < vs)) {
            size_t e = line.find("]22", ss + 7);
            if (e == std::string::npos) break;
            args.push_back(line.substr(ss + 7, e - ss - 7));
            p = e + 3;
        } else {
            size_t e = vs + 4;
            while (e < line.size() && (isalnum((unsigned char)line[e]) || line[e] == '_')) e++;
            std::string var_name = line.substr(vs + 4, e - vs - 4);

            bool all_digit = !var_name.empty();
            for (char c : var_name) if (!isdigit((unsigned char)c)) { all_digit = false; break; }

            if (all_digit) args.push_back(var_name);
            else           args.push_back(resolve_deep_value(var_name, mem));
            p = e;
        }
    }

    if (args.size() < 5) {
        std::cerr << "[XYCREATE] 参数不足，至少需要 5 个 (实际 " << args.size() << ")\n";
        return true;
    }

    // ===== 必填 =====
    std::string name  = args[0];
    std::string ext   = args[1];
    std::string magic = args[2];
    std::string bits  = args[3];

    int group = 7;
    if (!args[4].empty() && args[4] != "None") {
        bool all_digit = true;
        for (char c : args[4]) if (!isdigit((unsigned char)c)) { all_digit = false; break; }
        if (all_digit) group = std::stoi(args[4]);
    }

    // ===== 可选 owner / version =====
    std::string owner   = (args.size() >= 6 && !args[5].empty() && args[5] != "None") ? args[5] : "XingYuan";
    std::string version = (args.size() >= 7 && !args[6].empty() && args[6] != "None") ? args[6] : "v1.0.0";

    // ===== 调用全局 xyfile_write =====
    std::string path = ::xyfile_write(
        g_user_cwd, name, ext, bits, group,
        owner, version, magic
    );

    if (path.empty()) {
        std::cerr << "[XYCREATE] 创建失败: " << name << ext << "\n";
    }
    return true;
}