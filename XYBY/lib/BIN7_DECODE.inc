// ============================================================
// lib/BIN7_DECODE.inc —— 纯二进制 -> ASCII 字符
// 输入：二进制字符串（例如 "1001000..."）和 位数（7 或 8）
// 输出：解码后的 ASCII 字符串
// ============================================================
#pragma once
#include <string>
#include <iostream>
#include <cctype>

bool handleBin7Decode(const std::string& line, MemorySpace& mem) {
    if (line.find("FID:BIN7_DECODE") == std::string::npos) return false;

    // 1. 提取位数参数（BITS:7 或 BITS:8）
    int bits = 7;
    size_t bits_pos = line.find("BITS:");
    if (bits_pos != std::string::npos) {
        bits = std::stoi(line.substr(bits_pos + 5, 1));
    }

    // =========================================================
    // 【核心修复 1】只截取 FID:BIN7_DECODE 之后的内容，避开前面的 {VID:AST}
    // =========================================================
    size_t decode_pos = line.find("FID:BIN7_DECODE");
    std::string args_part = line.substr(decode_pos);

    // 2. 提取二进制数据（从 SID 或 VID）
    std::string binary_data = "";
    size_t sid = args_part.find("SID:22[");
    size_t vid = args_part.find("VID:");

    if (sid != std::string::npos) {
        // 字符串字面量
        size_t lb = sid + 7;
        size_t rb = args_part.find("]22", lb);
        if (rb != std::string::npos) binary_data = args_part.substr(lb, rb - lb);
    } else if (vid != std::string::npos) {
        // 变量（此时它只会找到真正的参数变量，比如 ast）
        size_t vs = vid + 4, ve = vs;
        while (ve < args_part.size() && (isalnum((unsigned char)args_part[ve]) || args_part[ve] == '_')) ve++;
        std::string var_name = args_part.substr(vs, ve - vs);
        binary_data = resolve_deep_value(var_name, mem);
    }

    if (binary_data == "None" || binary_data.empty()) {
        std::cerr << "[BIN7_DECODE] 错误: 二进制数据为空。" << std::endl;
        return true;
    }

    // =========================================================
    // 3. 核心：二进制 -> ASCII（每 bits 位一个字符）
    // =========================================================
    std::string source;
    for (size_t i = 0; i + bits <= binary_data.size(); i += bits) {
        unsigned char ch = 0;
        for (int j = 0; j < bits; ++j) {
            if (binary_data[i + j] == '1') ch |= (1 << (bits - 1 - j));
        }
        source += (char)ch;
    }


    // =========================================================
    // 【核心修复 2】提取赋值目标，兼容 `x = BIN7_DECODE(...)` 和 `... as x` 两种写法
    // =========================================================
    std::string as_var = "";
    
    // 先找 as 语法： AS:VID:xxx
    size_t as_pos = line.find("AS:VID:");
    if (as_pos != std::string::npos) {
        size_t s = as_pos + 7, e = s;
        while (e < line.size() && (isalnum((unsigned char)line[e]) || line[e] == '_')) e++;
        as_var = line.substr(s, e - s);
    } else {
        // 再找 [3D] 赋值语法： {VID:xxx}
        size_t curly_pos = line.find("{VID:");
        if (curly_pos != std::string::npos) {
            size_t s = curly_pos + 5, e = s;
            while (e < line.size() && (isalnum((unsigned char)line[e]) || line[e] == '_')) e++;
            as_var = line.substr(s, e - s);
        }
    }

    // 4. 赋值或直接打印
    if (!as_var.empty()) {
        Variable v;
        v.type = "STR";
        v.value = source;
        v.address = allocate_address(as_var);
        if (mem.copy_history.empty()) mem.copy_history.push_back({});
        mem.copy_history.back()[as_var] = v;
        mem.primary_memory[as_var] = v;
    } else {
        std::cout << source << std::endl;
    }
    return true;
}