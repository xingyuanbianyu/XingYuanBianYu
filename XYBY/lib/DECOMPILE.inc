// =====================================================================
// lib/DECOMPILE.inc - 虚进制 -> 指定进制数值（关键字：decompile）
// =====================================================================
#include <string>
#include <iostream>
#include <stdexcept>
#include <map>
#include <vector>
#include <cctype>

int _decompile_zerosToDigit(int zero_count) {
    if (zero_count < 1 || zero_count > 10) throw std::invalid_argument("零的数量超出范围");
    return zero_count - 1; 
}

std::string _decompile_toNumberString(const std::string& mode, const std::string& code) {
    if (code.empty()) return "";
    int base = 10;
    if (mode == "b") base = 2;
    else if (mode == "o") base = 8;
    else if (mode == "d") base = 10;
    else throw std::invalid_argument("未知的进制模式，只支持 b, o, d");

    std::string dec_str;
    int zero_count = 0;
    for (char c : code) {
        if (c == '0') { zero_count++; } 
        else if (c == '.' || c == ',') {
            if (zero_count > 0) dec_str += std::to_string(_decompile_zerosToDigit(zero_count));
            zero_count = 0;
        }
    }
    if (zero_count > 0) dec_str += std::to_string(_decompile_zerosToDigit(zero_count));

    if (mode == "d") return dec_str;
    long long value = 0;
    for (char c : dec_str) value = value * 10 + (c - '0');
    if (value == 0) return "0";

    std::string result;
    while (value > 0) { result = std::to_string(value % base) + result; value /= base; }
    return result;
}

bool handleDecompile(const std::string& line, MemorySpace& mem) {
    if (line.find("KID:DECOMPILE") == std::string::npos) return false;

    std::string code_str;
    size_t file_pos = line.find("FILE:VID:");
    size_t val_pos = line.find("VALUE:");
    
    if (file_pos != std::string::npos) {
        size_t s = file_pos + 9;
        size_t e = s;
        while (e < line.size() && (isalnum((unsigned char)line[e]) || line[e] == '_')) e++;
        std::string var_name = line.substr(s, e - s);
        std::string resolved = resolve_deep_value(var_name, mem);
        if (resolved.empty() || resolved == "None" || resolved == "False") {
            std::cerr << "DECOMPILE 错误: 内存中找不到变量 [" << var_name << "]" << std::endl;
            return true;
        }
        code_str = resolved;
    } 
    else if (val_pos != std::string::npos) {
        size_t val_start = val_pos + 6;
        size_t val_end = line.find(" AS:{KID:AS", val_start); 
        if (val_end == std::string::npos) val_end = line.find("}MEID:", val_start);
        code_str = line.substr(val_start, val_end - val_start);
        if (code_str.rfind("VID:", 0) == 0) code_str = code_str.substr(4);
        if (code_str.rfind("NID:", 0) == 0) code_str = code_str.substr(4);
        if (code_str.rfind("SID:", 0) == 0) code_str = code_str.substr(4);
        size_t sid_start = code_str.find("SID:22[");
        if (sid_start != std::string::npos) {
            size_t sid_end = code_str.find("]22", sid_start);
            if (sid_end != std::string::npos) code_str = code_str.substr(sid_start + 7, sid_end - (sid_start + 7));
        }
    } else { return true; }

    std::string mode = "d"; 
    size_t mode_pos = code_str.find("MODE:");
    if (mode_pos != std::string::npos) {
        mode = code_str.substr(mode_pos + 5, 1);
        code_str = code_str.substr(0, mode_pos);
    } else {
        size_t space_pos = code_str.find(' ');
        if (space_pos != std::string::npos) {
            std::string first_part = code_str.substr(0, space_pos);
            if (first_part == "b" || first_part == "o" || first_part == "d") { mode = first_part; code_str = code_str.substr(space_pos + 1); }
        }
    }

    std::string compiled;
    try {
        std::vector<std::string> parts;
        std::string current_part;
        for (char c : code_str) {
            if (c == ' ' || c == '\t') { if (!current_part.empty()) { parts.push_back(current_part); current_part.clear(); } } 
            else current_part += c;
        }
        if (!current_part.empty()) parts.push_back(current_part);

        for (size_t i = 0; i < parts.size(); ++i) {
            std::string part_res = _decompile_toNumberString(mode, parts[i]);
            compiled += part_res;
            if (i < parts.size() - 1) compiled += " ";
        }
    } catch (const std::exception& e) {
        std::cerr << "DECOMPILE 异常: " << e.what() << std::endl;
        return true;
    }

    std::string as_target = extractAsVar(line);
    if (!as_target.empty()) {
        assignToTarget(as_target, compiled, mem);
    } else {
        std::cout << compiled << std::endl;
    }
    return true;
}