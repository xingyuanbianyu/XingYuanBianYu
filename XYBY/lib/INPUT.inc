// ============================================================
// INPUT.cpp —— FID:INPUT 识别 + 交互求值（防死锁版）
// ============================================================
#include <unistd.h>  // 🚀 引入 isatty，需要系统支持

bool isInputExpression(const std::string& s) {
    if (s.rfind("FID:INPUT", 0) != 0) return false;
    if (s.find("[ADD]") != std::string::npos) return false;
    if (s.find("[SUB]") != std::string::npos) return false;
    if (s.find("[MUL]") != std::string::npos) return false;
    if (s.find("[DIV]") != std::string::npos) return false;
    return true;
}

std::string evalInputExpression(const std::string& s) {
    std::string type = "SID";
    size_t t_start = s.find("TYPE:");
    if (t_start != std::string::npos) {
        size_t t_end = s.find(" ", t_start);
        if (t_end == std::string::npos) t_end = s.length();
        type = s.substr(t_start + 5, t_end - t_start - 5);
    }

    std::string prompt = "";
    size_t p_start = s.find("PROMPT:SID:22[");
    if (p_start != std::string::npos) {
        size_t p_end = s.find("]22", p_start);
        if (p_end != std::string::npos) {
            prompt = s.substr(p_start + 14, p_end - p_start - 14);
        }
    }

    // 还原换行占位符：\x01 → \n
    for (char& ch : prompt) if (ch == '\x01') ch = '\n';

    // ============================================================
    // 🚀 核心防死锁护盾：检测是否在前台终端运行
    // ============================================================
    if (!isatty(STDIN_FILENO)) {
        std::cout << prompt;
        std::cout << "\n\033[1;33m[系统警告] 后台环境无法读取键盘输入，自动跳过。\033[0m" << std::endl;
        return "None"; // 强制返回 None，打断交互，防止卡死
    }

    // 以下代码只有在 Termux 终端直接运行时才会执行
    std::cout << prompt;
    std::cout.flush();
    std::string input_val;
    std::getline(std::cin, input_val);

    if (type == "NID") {
        bool valid = true;
        if (input_val.empty()) valid = false;
        else {
            size_t start = 0;
            if (input_val[0] == '-') start = 1;
            if (start == input_val.length()) valid = false;
            for (size_t k = start; k < input_val.length(); ++k) {
                if (!::isdigit(input_val[k])) { valid = false; break; }
            }
        }
        if (!valid) {
            std::cout << "Error: Cannot convert '" << input_val << "' to int" << std::endl;
            return "Error";
        }
        if (input_val[0] == '-') {
            std::string pos_part = input_val.substr(1);
            size_t first_non_zero = pos_part.find_first_not_of('0');
            if (first_non_zero == std::string::npos) input_val = "0";
            else input_val = "-" + pos_part.substr(first_non_zero);
        } else {
            size_t first_non_zero = input_val.find_first_not_of('0');
            if (first_non_zero == std::string::npos) input_val = "0";
            else input_val = input_val.substr(first_non_zero);
        }
        return input_val;
    }
    else if (type == "FLT") {
        char* end = nullptr;
        std::strtod(input_val.c_str(), &end);
        if (end == input_val.c_str() || *end != '\0') {
            std::cout << "Error: Cannot convert '" << input_val << "' to float" << std::endl;
            return "Error";
        }
        return format_double(std::stod(input_val));
    }
    return input_val;
}