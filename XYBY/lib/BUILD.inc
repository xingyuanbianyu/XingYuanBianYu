// =====================================================================
// lib/BUILD.inc - 指定进制数字 -> 虚进制（智能映射版 + 二进制分组）
// =====================================================================
#include <string>
#include <iostream>
#include <stdexcept>
#include <map>
#include <vector>
#include <cctype>

std::string _build_digitToZeros(int digit) {
    if (digit < 0 || digit > 9) throw std::invalid_argument("虚进制峰值只有九（0-9）");
    return std::string(digit + 1, '0');
}

std::string _build_directMap(const std::string& input) {
    std::string output;
    for (size_t i = 0; i < input.size(); ++i) {
        int digit = input[i] - '0';
        output += _build_digitToZeros(digit);
        if (i < input.size() - 1) output += ".";
    }
    output += ",";
    return output;
}

std::string _build_autoCarry(const std::string& input, int base) {
    std::vector<int> digits;
    for (char c : input) digits.push_back(c - '0');

    int carry = 0;
    for (int i = (int)digits.size() - 1; i >= 0; --i) {
        int val = digits[i] + carry;
        if (val >= base) { val -= base; carry = 1; } else { carry = 0; }
        digits[i] = val;
    }
    if (carry > 0) digits.insert(digits.begin(), carry);

    std::string output;
    for (size_t i = 0; i < digits.size(); ++i) {
        output += _build_digitToZeros(digits[i]);
        if (i < digits.size() - 1) output += ".";
    }
    output += ",";
    return output;
}

std::string _build_numericConvert(const std::string& input, int base) {
    long long value = 0;
    for (char c : input) {
        if (!isdigit((unsigned char)c)) throw std::invalid_argument("非法字符");
        value = value * 10 + (c - '0');
    }

    std::vector<int> digits;
    if (value == 0) {
        digits.push_back(0);
    } else {
        while (value > 0) {
            digits.insert(digits.begin(), value % base);
            value /= base;
        }
    }

    std::string output;
    for (size_t i = 0; i < digits.size(); ++i) {
        output += _build_digitToZeros(digits[i]);
        if (i < digits.size() - 1) output += ".";
    }
    output += ",";
    return output;
}

// =====================================================================
// 核心编译函数
// =====================================================================
std::string _build_compileNumber(const std::string& mode, const std::string& input, bool force_numeric, int bits = 8) {
    if (input.empty()) return "";

    int base = 10;
    if (mode == "b") base = 2;
    else if (mode == "o") base = 8;
    else if (mode == "d") base = 10;
    else throw std::invalid_argument("未知的进制模式，只支持 b, o, d");

    if (force_numeric) return _build_numericConvert(input, base);

    // 二进制模式：宽容过滤
    if (mode == "b") {
        std::vector<char> digits;
        for (char c : input) {
            if (c == '0' || c == '1') {
                digits.push_back(c);
            } else {
                if (isprint((unsigned char)c) &&
                    c != ' ' && c != '\t' && c != '\n' && c != '\r' &&
                    c != '.' && c != ',') {
                    std::cerr << "[BUILD] 二进制输入中发现非 0/1 可见字符: '"
                              << c << "' (0x" << std::hex << (int)(unsigned char)c << std::dec
                              << ")，已跳过\n";
                }
                continue;
            }
        }

        std::string output;
        for (size_t i = 0; i < digits.size(); ++i) {
            if (digits[i] == '0') output += "0";
            else                  output += "00";

            if (i < digits.size() - 1) {
                if ((i + 1) % bits == 0) output += ",";
                else                     output += ".";
            }
        }
        output += ",";
        return output;
    }

    bool all_valid = true;
    for (char c : input) {
        if ((c - '0') >= base) { all_valid = false; break; }
    }

    if (all_valid) return _build_directMap(input);
    else           return _build_autoCarry(input, base);
}

// =====================================================================
// BUILD 处理器（适配新的 AST 格式）
//
// 新格式：
//   ID:{KID:BUILD MODE:SID:22[b]22 VALUE:VID:aST AS:{KID:AS VALUE:VID:AST}}MEID:LID:5
//   ID:{KID:BUILD MODE:VID:m  VALUE:VID:data AS:{KID:AS VALUE:VID:OUT}}MEID:LID:5
//
// 解析顺序：先 MODE，再 VALUE，最后 AS
// =====================================================================
bool handleBuild(const std::string& line, MemorySpace& mem, 
                 std::map<int, bool>& if_passed, int brace_level, 
                 int skip_level, const std::vector<std::string>& lines, size_t line_idx) {
    
    if (line.find("KID:BUILD") == std::string::npos) return false;

    std::string mode = "d";
    std::string value_node = "";
    int bits = 8;

    // ============================================================
    // 1) 解析 MODE 段
    //    形式一：MODE:SID:22[x]22
    //    形式二：MODE:VID:xxx
    // ============================================================
    size_t mode_pos = line.find("MODE:");
    if (mode_pos != std::string::npos) {
        size_t after = mode_pos + 5;

        if (line.compare(after, 7, "SID:22[") == 0) {
            // 硬编码模式：MODE:SID:22[b]22
            size_t ch_pos = after + 7;
            if (ch_pos < line.size()) mode = std::string(1, line[ch_pos]);
        }
        else if (line.compare(after, 4, "VID:") == 0) {
            // 变量模式：MODE:VID:xxx
            size_t vs = after + 4;
            size_t ve = vs;
            while (ve < line.size() &&
                   (isalnum((unsigned char)line[ve]) || line[ve] == '_')) ve++;
            std::string mode_var = line.substr(vs, ve - vs);

            // 查变量里的实际值
            std::string resolved = resolve_deep_value(mode_var, mem);
            if (!resolved.empty() && resolved != "None") {
                mode = resolved.substr(0, 1);
            }
        }
    }

    // ============================================================
    // 2) 解析 VALUE 段（从 "VALUE:" 到 " AS:" 或 "}MEID:" 之间）
    // ============================================================
    size_t val_pos = line.find("VALUE:");
    if (val_pos == std::string::npos) {
        // 没有 VALUE 字段，直接返回
        std::cerr << "[BUILD] 未找到 VALUE 字段\n";
        return true;
    }
    size_t val_start = val_pos + 6;

    size_t val_end = line.find(" AS:{KID:AS", val_start);
    if (val_end == std::string::npos) val_end = line.find("}MEID:", val_start);
    if (val_end == std::string::npos) val_end = line.size();

    value_node = line.substr(val_start, val_end - val_start);

    // 去掉尾部可能的空格
    while (!value_node.empty() &&
           (value_node.back() == ' ' || value_node.back() == '\t' ||
            value_node.back() == '\n' || value_node.back() == '\r')) {
        value_node.pop_back();
    }
    // 去掉头部空格
    while (!value_node.empty() &&
           (value_node.front() == ' ' || value_node.front() == '\t')) {
        value_node.erase(0, 1);
    }

    // ============================================================
    // 3) 剥离 VID:/NID:/SID: 前缀（大小写无关）
    // ============================================================
    auto strip_prefix = [](std::string& s, const std::string& prefix) {
        if (s.size() >= prefix.size()) {
            bool match = true;
            for (size_t i = 0; i < prefix.size(); ++i) {
                if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) {
                    match = false; break;
                }
            }
            if (match) s = s.substr(prefix.size());
        }
    };
    strip_prefix(value_node, "VID:");
    strip_prefix(value_node, "NID:");
    strip_prefix(value_node, "SID:");

    // 去掉前缀后再 trim
    while (!value_node.empty() &&
           (value_node.front() == ' ' || value_node.front() == '\t')) {
        value_node.erase(0, 1);
    }
    while (!value_node.empty() &&
           (value_node.back() == ' ' || value_node.back() == '\t')) {
        value_node.pop_back();
    }

    // ============================================================
    // 4) 解析 bits（二进制模式可选 7 或 8）
    // ============================================================
    if (mode == "b") {
        size_t sp = value_node.find(' ');
        if (sp != std::string::npos) {
            std::string bit_str = value_node.substr(0, sp);
            if (bit_str == "7" || bit_str == "8") {
                bits = std::stoi(bit_str);
                value_node = value_node.substr(sp + 1);
            }
        }
    }

    // ============================================================
    // 5) 解析 -t 强制转换标记
    // ============================================================
    bool force_numeric = false;
    size_t t_pos = value_node.find("-t");
    if (t_pos != std::string::npos) {
        force_numeric = true;
        value_node.erase(t_pos, 2);
        // 去掉 -t 后面可能留下的空格
        while (t_pos < value_node.size() &&
               (value_node[t_pos] == ' ' || value_node[t_pos] == '\t')) {
            value_node.erase(t_pos, 1);
        }
    }

    // ============================================================
    // 6) 切分 value_node 成多个部分，逐个解析
    // ============================================================
    std::string compiled;
    try {
        std::vector<std::string> parts;
        std::string current_part;
        for (char c : value_node) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                if (!current_part.empty()) { parts.push_back(current_part); current_part.clear(); }
            } else current_part += c;
        }
        if (!current_part.empty()) parts.push_back(current_part);

        for (size_t i = 0; i < parts.size(); ++i) {
            std::string current_arg = parts[i];
            // 尝试从变量表里查值
            std::string resolved = resolve_deep_value(current_arg, mem);
            if (!resolved.empty() && resolved != "None" && resolved != "False" && resolved != current_arg) {
                current_arg = resolved;
            }

            std::string part_compiled = _build_compileNumber(mode, current_arg, force_numeric, bits);
            if (!part_compiled.empty() && part_compiled.back() == ',') part_compiled.pop_back();
            compiled += part_compiled;
            if (i < parts.size() - 1) compiled += ",";
        }
    } catch (const std::exception& e) {
        std::cerr << "BUILD 异常: " << e.what() << std::endl;
        return true;
    }

    // ============================================================
    // 7) 存入目标变量 或 打印
    // ============================================================
    std::string as_target = extractAsVar(line);
    if (!as_target.empty()) {
        assignToTarget(as_target, compiled, mem);
    } else {
        std::cout << compiled << std::endl;
    }
    return true;
}