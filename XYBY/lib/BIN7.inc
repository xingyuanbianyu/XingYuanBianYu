// ============================================================
// lib/BIN7.inc —— 字符 -> 7 位 ASCII 二进制
// 关键字形式：BIN7(...) 或 BIN7 ...
// 支持变量和字符串字面量，所有字符（含数字）统一转 7 位
// ============================================================
#pragma once
#include <string>
#include <iostream>
#include <cctype>

// AST 节点处理器
// 节点格式: ID:{FID:BIN7 ARG:SID:22[...]22}MEID:LID:N
//           ID:{FID:BIN7 ARG:VID:var_name}MEID:LID:N
// 或者赋值形式: ID:{PID:0x24{VID:x}CONST:FALSE[3D]FID:BIN7 ARG:...}MEID:LID:N
bool handleBin7(const std::string& line, MemorySpace& mem) {
    if (line.find("FID:BIN7") == std::string::npos) return false;

    size_t bin7_pos = line.find("FID:BIN7");

    // =========================================================
    // 🚀 赋值目标提取：只在 FID:BIN7 之前找 {VID:xxx}
    //    这样绝不会命中 BIN7 参数里的 VID
    // =========================================================
    std::string as_var = "";
    {
        std::string before_bin7 = line.substr(0, bin7_pos);
        size_t as_pos = before_bin7.find("{VID:");
        if (as_pos != std::string::npos) {
            size_t s = as_pos + 5;
            size_t e = s;
            while (e < before_bin7.size() &&
                   (isalnum((unsigned char)before_bin7[e]) || before_bin7[e] == '_')) {
                e++;
            }
            as_var = before_bin7.substr(s, e - s);
        }
    }

    // =========================================================
    // BIN7 参数提取：只在 FID:BIN7 之后找
    // =========================================================
    std::string args_part = line.substr(bin7_pos + 8);

    std::string arg = "";
    size_t sid = args_part.find("SID:22[");
    size_t vid = args_part.find("VID:");

    // 优先 SID 字符串字面量
    if (sid != std::string::npos) {
        size_t lb = sid + 7;
        size_t rb = args_part.find("]22", lb);
        if (rb != std::string::npos) arg = args_part.substr(lb, rb - lb);
    }
    // 其次 VID 变量
    else if (vid != std::string::npos) {
        size_t vs = vid + 4, ve = vs;
        while (ve < args_part.size() &&
               (isalnum((unsigned char)args_part[ve]) || args_part[ve] == '_')) ve++;
        std::string var_name = args_part.substr(vs, ve - vs);
        arg = resolve_deep_value(var_name, mem);
    } else {
    }


    // 全部字符转 7 位 ASCII 二进制（数字也转）
    std::string result;
    for (char c : arg) {
        unsigned char uc = (unsigned char)c;
        for (int i = 6; i >= 0; --i) {
            result += ((uc >> i) & 1) ? '1' : '0';
        }
    }


    if (!as_var.empty()) {
        // 有赋值目标 -> 存进变量
        Variable v;
        v.type    = "STR";
        v.value   = result;
        v.address = allocate_address(as_var);
        if (mem.copy_history.empty()) mem.copy_history.push_back({});
        mem.copy_history.back()[as_var] = v;
        mem.primary_memory[as_var]      = v;
    } else {
        // 没有赋值目标 -> 直接打印
        std::cout << result << std::endl;
    }
    return true;
}