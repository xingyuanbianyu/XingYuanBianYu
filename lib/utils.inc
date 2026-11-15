// =================================================================
// utils.inc —— 基础工具与路径管理
// =================================================================
std::unordered_map<std::string, std::string> MOUNTS = {};

// 全局 COOID 表（注释锚点原文，按出现顺序索引）
std::vector<std::string> g_cooid_table;

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(' ');
    if (std::string::npos == first) return "";
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}

// ============================================================
// XY 自定义转义字符处理（仅作用于字符串字面量内部）
// ============================================================
std::string process_escapes(const std::string& s) {
    std::string out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];

        if (c != '\\' || i + 1 >= s.size()) {
            out += c;
            continue;
        }

        char next = s[i + 1];

        // \r \n → 换行占位符
        if (next == 'r' || next == 'n') {
            out += '\x01';
            i += 1;
            continue;
        }

        // \x → 彩蛋（仅当后面不是标识符时）
        if (next == 'x') {
            size_t j = i + 2;
            bool next_is_ident = (j < s.size() &&
                (isalnum((unsigned char)s[j]) || s[j] == '_'));
            if (!next_is_ident) {
                out += "Hello World";
                i += 1;
                continue;
            }
            out += "\\x";
            i += 1;
            continue;
        }

        // \\ → 字面反斜杠
        if (next == '\\') {
            out += '\\';
            i += 1;
            continue;
        }

        // \uXXXX → Unicode → UTF-8
        if (next == 'u') {
            size_t j = i + 2;
            std::string hex;
            while (j < s.size() && isxdigit((unsigned char)s[j]) && hex.size() < 8) {
                hex += s[j]; j++;
            }
            if (!hex.empty()) {
                try {
                    unsigned long cp = std::stoul(hex, nullptr, 16);
                    if (cp < 0x80) {
                        out += (char)cp;
                    } else if (cp < 0x800) {
                        out += (char)(0xC0 | (cp >> 6));
                        out += (char)(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        out += (char)(0xE0 | (cp >> 12));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    } else if (cp < 0x110000) {
                        out += (char)(0xF0 | (cp >> 18));
                        out += (char)(0x80 | ((cp >> 12) & 0x3F));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    }
                    i = j - 1;
                } catch (...) { out += c; }
                continue;
            }
            out += c;
            continue;
        }

        // \b \o \h \d → 进制字面量转十进制
        if (next == 'b' || next == 'o' || next == 'h' || next == 'd') {
            int base = (next == 'b') ? 2 : (next == 'o') ? 8 : (next == 'h') ? 16 : 10;
            size_t j = i + 2;
            std::string digits;
            while (j < s.size()) {
                char d = s[j];
                bool ok = false;
                if      (next == 'b') ok = (d == '0' || d == '1');
                else if (next == 'o') ok = (d >= '0' && d <= '7');
                else if (next == 'h') ok = isxdigit((unsigned char)d);
                else                  ok = isdigit((unsigned char)d);
                if (!ok) break;
                digits += d; j++;
            }
            if (!digits.empty()) {
                try {
                    unsigned long long v = std::stoull(digits, nullptr, base);
                    out += std::to_string(v);
                    i = j - 1;
                    continue;
                } catch (...) {}
            }
            out += c;
            continue;
        }

        out += c;  // 未知转义原样保留
    }
    return out;
}

// =================================================================
// 中文关键字映射（带字符串状态机）
// =================================================================
std::string map_chinese_to_english(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    bool in_string = false;
    char quote_char = 0;

    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (!in_string) {
            if (c == '"' || c == '\'') { in_string = true; quote_char = c; result += c; continue; }
        } else {
            if (c == quote_char && (i == 0 || input[i - 1] != '\\')) { in_string = false; result += c; continue; }
        }
        if (in_string) { result += c; continue; }

        bool matched = false;
        for (const auto& pair : LANG_ZH_MAP) {
            const std::string& zh = pair.second;
            if (input.compare(i, zh.length(), zh) == 0) {
                bool left_bound = (i == 0 || !isalnum((unsigned char)input[i - 1]));
                bool right_bound = (i + zh.length() == input.length() || !isalnum((unsigned char)input[i + zh.length()]));
                if (left_bound && right_bound) {
                    result += pair.first; i += zh.length() - 1; matched = true; break;
                }
            }
        }
        if (!matched) result += c;
    }
    return result;
}

// =================================================================
// 注释剥离器
// =================================================================
std::string strip_inline_comments(const std::string& line, 
                                   const std::map<std::string, std::vector<std::string>>& modules) {
    std::string result;
    result.reserve(line.size());
    bool in_string = false;
    char quote_char = 0;
    int paren_depth = 0;

    auto is_word_char = [](char c) -> bool {
        unsigned char uc = (unsigned char)c;
        return isalnum(uc) || uc == '_' || uc >= 0x80;
    };

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (!in_string) {
            if (c == '"' || c == '\'') { in_string = true; quote_char = c; }
            if (c == '(') paren_depth++;
            else if (c == ')') paren_depth--;
        } else {
            if (c == quote_char && (i == 0 || line[i-1] != '\\')) in_string = false;
            result += c; continue;
        }
        if (in_string) { result += c; continue; }

        if (c == '$') {
            if (i + 1 < line.size() && line[i+1] == '{') { result += c; continue; }
            else break;
        }

        if (c == '#') {
            size_t next_pos = i + 1;
            while (next_pos < line.size() && isspace((unsigned char)line[next_pos])) next_pos++;
            std::string rest = line.substr(next_pos);
            if (rest.rfind("include", 0) == 0) {
                result += line.substr(i);
                break;
            } else break;
        }

        if (c == '/' && i + 1 < line.size() && line[i+1] == '/') {
            if (paren_depth > 0) { result += c; continue; }
            size_t prev = i;
            while (prev > 0 && isspace((unsigned char)line[prev-1])) prev--;
            bool has_left = (prev > 0) && is_word_char(line[prev-1]);
            size_t next = i + 2;
            while (next < line.size() && isspace((unsigned char)line[next])) next++;
            bool has_right = (next < line.size()) && is_word_char(line[next]);
            if (has_left && has_right) { result += c; continue; }
            else break;
        }

        if (c == '!' && i + 1 < line.size() && line[i+1] == 's') {
            std::string rest = line.substr(i + 2);
            size_t eq_pos = rest.find('=');
            if (eq_pos != std::string::npos) {
                std::string assignment = trim(rest.substr(0, eq_pos + 1)) + " " + trim(rest.substr(eq_pos + 1));
                result += assignment;
            }
            break; 
        }

        if (c == '@') {
            size_t j = i + 1;
            while (j < line.size() && (isalnum((unsigned char)line[j]) || line[j] == '_')) j++;
            std::string symbol = line.substr(i + 1, j - i - 1);
            bool is_imported = false;
            for (const auto& pair : modules) {
                if (pair.first == symbol) { is_imported = true; break; }
            }
            if (is_imported) { result += c; continue; }
            else break;
        }
        result += c;
    }
    return trim(result);
}

bool is_gui_available() {
    const char* display = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    if ((display && strlen(display) > 0) || (wayland && strlen(wayland) > 0)) return true;
    return false;
}

std::string get_prompt(const std::string& current_cwd) {
    if (current_cwd.empty()) return ">>> ";
    std::string display_path = VFS::getInstance().get_display_path(current_cwd);
    if (display_path.empty() || display_path == "/" || display_path == "\\") return ">>> ";
    return display_path + ">> ";
}

std::string read_module_source(const std::string& module_name) {
    std::vector<std::string> candidates = {
        module_name, module_name + ".xyby", module_name + ".xyd",
        module_name + ".xyb", module_name + ".xy",
    };
    for (const auto& path : candidates) {
        std::ifstream ifs(path, std::ios::binary);
        if (ifs.is_open()) {
            std::stringstream buf; buf << ifs.rdbuf(); return buf.str();
        }
    }
    return "";
}

// =================================================================
// 块注释处理器
// =================================================================
std::string process_block_comments(const std::string& code) {
    std::string result;
    result.reserve(code.size());

    enum State { NONE, STRING_DOUBLE, STRING_SINGLE, C_STYLE, TRIPLE_DOUBLE, TRIPLE_SINGLE };
    State state = NONE;
    char quote_char = 0;
    size_t current_cooid_idx = 0;

    auto finalize_cooid = [&]() {
        std::string& buf = g_cooid_table[current_cooid_idx];
        std::stringstream ss(buf);
        std::string ln;
        std::string cleaned;
        bool first = true;
        while (std::getline(ss, ln)) {
            size_t a = ln.find_first_not_of(" \t\r");
            if (a == std::string::npos) continue;
            size_t b = ln.find_last_not_of(" \t\r");
            std::string piece = ln.substr(a, b - a + 1);
            if (!first) cleaned += "\n";
            cleaned += piece;
            first = false;
        }
        buf = cleaned;
    };

    for (size_t i = 0; i < code.size(); ++i) {
        char c = code[i];

        switch (state) {
            case NONE: {
                if (c == '"') {
                    if (i + 2 < code.size() && code[i+1] == '"' && code[i+2] == '"') {
                        current_cooid_idx = g_cooid_table.size();
                        g_cooid_table.push_back("");
                        result += "COOID_" + std::to_string(current_cooid_idx);
                        state = TRIPLE_DOUBLE;
                        i += 2;
                    } else {
                        state = STRING_DOUBLE;
                        quote_char = '"';
                        result += c;
                    }
                }
                else if (c == '\'') {
                    if (i + 2 < code.size() && code[i+1] == '\'' && code[i+2] == '\'') {
                        current_cooid_idx = g_cooid_table.size();
                        g_cooid_table.push_back("");
                        result += "COOID_" + std::to_string(current_cooid_idx);
                        state = TRIPLE_SINGLE;
                        i += 2;
                    } else {
                        state = STRING_SINGLE;
                        quote_char = '\'';
                        result += c;
                    }
                }
                else if (c == '/' && i + 1 < code.size() && code[i+1] == '*') {
                    current_cooid_idx = g_cooid_table.size();
                    g_cooid_table.push_back("");
                    result += "COOID_" + std::to_string(current_cooid_idx);
                    state = C_STYLE;
                    i++;
                }
                else {
                    result += c;
                }
                break;
            }

            case STRING_SINGLE:
            case STRING_DOUBLE: {
                result += c;
                if (c == quote_char && (i == 0 || code[i-1] != '\\')) {
                    state = NONE;
                }
                break;
            }

            case C_STYLE: {
                if (c == '*' && i + 1 < code.size() && code[i+1] == '/') {
                    finalize_cooid();
                    state = NONE;
                    i++;
                } else {
                    g_cooid_table[current_cooid_idx] += c;
                    if (c == '\n') result += '\n';
                }
                break;
            }

            case TRIPLE_DOUBLE: {
                if (c == '"' && i + 2 < code.size() && code[i+1] == '"' && code[i+2] == '"') {
                    finalize_cooid();
                    state = NONE;
                    i += 2;
                } else {
                    g_cooid_table[current_cooid_idx] += c;
                    if (c == '\n') result += '\n';
                }
                break;
            }

            case TRIPLE_SINGLE: {
                if (c == '\'' && i + 2 < code.size() && code[i+1] == '\'' && code[i+2] == '\'') {
                    finalize_cooid();
                    state = NONE;
                    i += 2;
                } else {
                    g_cooid_table[current_cooid_idx] += c;
                    if (c == '\n') result += '\n';
                }
                break;
            }
        }
    }
    return result;
}