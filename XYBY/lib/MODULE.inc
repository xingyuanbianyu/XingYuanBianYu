// =====================================================================
// MODULE.inc - 模块头定义解析器（傻瓜包容版）
// =====================================================================
#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <cctype>

struct ModuleHeader {
    std::string type;        // RETR, STRG, ASRT, CHLY, OSIF
    std::string prefix_rule; // FFT, ELF, OIT, SFT
    std::string origin;      // SOF, NIT
    bool valid = false;
};

// 去除首尾空白
std::string _module_trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

ModuleHeader parse_module_header(const std::vector<std::string>& lines) {
    ModuleHeader header;
    
    std::vector<std::string> valid_lines;
    for (const auto& l : lines) {
        std::string t = _module_trim(l);
        if (!t.empty()) valid_lines.push_back(t);
        if (valid_lines.size() >= 3) break;
    }

    if (valid_lines.size() < 3) {
        std::cerr << "[MODULE] 错误: 模块文件不足3行有效的元数据头部。" << std::endl;
        return header;
    }

    // 调试打印，看看到底读到了什么
    std::cerr << "[MODULE-DBG] 第1行: [" << valid_lines[0] << "]" << std::endl;
    std::cerr << "[MODULE-DBG] 第2行: [" << valid_lines[1] << "]" << std::endl;
    std::cerr << "[MODULE-DBG] 第3行: [" << valid_lines[2] << "]" << std::endl;

    // 第1行：模块类型（只看包含，不讲道理）
    std::string l1 = valid_lines[0];
    if (l1.find("RETR") != std::string::npos) header.type = "RETR";
    else if (l1.find("STRG") != std::string::npos) header.type = "STRG";
    else if (l1.find("ASRT") != std::string::npos) header.type = "ASRT";
    else if (l1.find("CHLY") != std::string::npos) header.type = "CHLY";
    else if (l1.find("OSIF") != std::string::npos) header.type = "OSIF";
    else { std::cerr << "[MODULE] 错误: 第1行没找到模块类型关键词" << std::endl; return header; }

    // 第2行：前后缀
    std::string l2 = valid_lines[1];
    if (l2.find("FFT") != std::string::npos) header.prefix_rule = "FFT";
    else if (l2.find("ELF") != std::string::npos) header.prefix_rule = "ELF";
    else if (l2.find("OIT") != std::string::npos) header.prefix_rule = "OIT";
    else if (l2.find("SFT") != std::string::npos) header.prefix_rule = "SFT";
    else { std::cerr << "[MODULE] 错误: 第2行没找到前缀定义关键词" << std::endl; return header; }

    // 第3行：出品方
    std::string l3 = valid_lines[2];
    if (l3.find("SOF") != std::string::npos) header.origin = "SOF";
    else if (l3.find("NIT") != std::string::npos) header.origin = "NIT";
    else { std::cerr << "[MODULE] 错误: 第3行没找到出品方关键词" << std::endl; return header; }

    header.valid = true;
    return header;
}