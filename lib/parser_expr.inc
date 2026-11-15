// =================================================================
// parser_expr.inc —— 表达式与值解析
// =================================================================

// 🚀 前向声明：encode_arg 需要递归调用 parse_expression
std::string parse_expression(const std::string& expr);

std::string parse_template_string(const std::string& inner) {
    std::string out = "TMPL:";
    size_t pos = 0;
    while (pos < inner.size()) {
        size_t p_dollar = inner.find("${", pos);
        size_t p_lt = inner.find('<', pos);
        
        size_t next_p = std::string::npos;
        bool is_dollar = false;
        if (p_dollar != std::string::npos && p_lt != std::string::npos) {
            if (p_dollar < p_lt) { next_p = p_dollar; is_dollar = true; }
            else { next_p = p_lt; is_dollar = false; }
        } else if (p_dollar != std::string::npos) {
            next_p = p_dollar; is_dollar = true;
        } else if (p_lt != std::string::npos) {
            next_p = p_lt; is_dollar = false;
        } else {
            if (pos < inner.size()) {
                out += "SID:22[" + inner.substr(pos) + "]22";
            }
            break;
        }
        
        if (next_p > pos) {
            out += "SID:22[" + inner.substr(pos, next_p - pos) + "]22";
        }
        
        if (is_dollar) {
            size_t e = inner.find('}', next_p);
            if (e == std::string::npos) {
                out += "SID:22[" + inner.substr(next_p) + "]22";
                break;
            }
            std::string var = trim(inner.substr(next_p + 2, e - next_p - 2));
            out += "VID:" + var;
            pos = e + 1;
        } else {
            size_t e = inner.find('>', next_p);
            if (e == std::string::npos) {
                out += "SID:22[" + inner.substr(next_p) + "]22";
                break;
            }
            std::string var = trim(inner.substr(next_p + 1, e - next_p - 1));
            out += "VID:" + var;
            pos = e + 1;
        }
    }
    return out;
}

// ============================================================
// 参数编码器
//   - 字符串字面量 → SID:22[...]22
//   - 纯数字       → NID:xxx / FLT:xxx
//   - 🚀 含运算符  → 递归 parse_expression（生成完整 AST）
//   - 纯标识符     → VID:xxx
// ============================================================
std::string encode_arg(const std::string& a) {
    // 1. 字符串字面量
    if (a.size() >= 2 && (a.front() == '"' || a.front() == '\'') && a.back() == a.front()) {
        std::string inner = a.substr(1, a.size() - 2);
        inner = process_escapes(inner);
        return "SID:22[" + inner + "]22";
    }

    // 2. 纯数字
    if (!a.empty() && (isdigit((unsigned char)a[0]) || a[0] == '-')) {
        bool all_digit = true;
        size_t start = (a[0] == '-') ? 1 : 0;
        for (size_t i = start; i < a.size(); ++i) {
            if (!isdigit((unsigned char)a[i]) && a[i] != '.') { all_digit = false; break; }
        }
        if (all_digit && start < a.size()) {
            if (a.find('.') != std::string::npos) return "FLT:" + a;
            if (a[0] == '-') return "NID:-" + a.substr(1);
            std::string pad = a;
            while (pad.length() < 4) pad = "0" + pad;
            return "NID:" + pad;
        }
    }

    // ============================================================
    // 🚀 3. 含运算符 → 递归 parse_expression
    //    这样 n-1、x*2、a+b 这类表达式参数能生成完整的 AST
    //    fact(n-1) 的 ARGS 从 "VID:n -1" 变成 "VID:n[SUB]NID:0001"
    // ============================================================
    {
        bool has_op = false;
        for (char c : a) {
            if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
                c == '(' || c == ')' || c == '[' || c == ']' ||
                c == '<' || c == '>' || c == '=' || c == '!' ||
                c == '&' || c == '|' || c == ',') {
                has_op = true; break;
            }
        }
        if (has_op) {
            return parse_expression(a);
        }
    }

    // 4. 纯标识符
    return "VID:" + a;
}

// ============================================================
// 内置函数独立识别器
// ============================================================
std::string try_builtin_call(const std::string& s) {
    if (s.empty() || s.back() != ')') return "";

    size_t lp = s.find('(');
    if (lp == std::string::npos || lp == 0) return "";

    // ============================================================
    // 🚀 关键修复：第一个 '(' 必须正好匹配到字符串末尾
    //    否则 add(50,50)*add(50,50) 会被误当成"一个函数调用"，
    //    参数变成 "50,50)*add(50,50" 这种半截东西。
    // ============================================================
    {
        int depth = 0;
        for (size_t i = lp; i < s.size(); ++i) {
            if (s[i] == '(') depth++;
            else if (s[i] == ')') {
                depth--;
                if (depth == 0 && i != s.size() - 1) return "";   // 后面还有东西 → 拒绝
            }
        }
        if (depth != 0) return "";   // 括号不闭合
    }

    std::string fn = trim(s.substr(0, lp));
    if (fn.empty()) return "";
    for (char& c : fn) c = (char)std::tolower((unsigned char)c);

    std::string arg = trim(s.substr(lp + 1, s.size() - lp - 2));

    static const std::map<std::string, std::string> BUILTINS = {
        {"len",  "FID:LEN"},
        {"abs",  "FID:ABS"},
        {"str",  "FID:STR"},
        {"int",  "FID:INT"},
        {"bool", "FID:BOOL"},
        {"hex",  "FID:HEX"},
        {"bin",  "FID:BIN"},
        {"oct",  "FID:OCT"},
        {"chr",  "FID:CHR"},
        {"ord",  "FID:ORD"},
        {"round", "FID:ROUND"},
        {"floor", "FID:FLOOR"},
        {"ceil",  "FID:CEIL"},
        {"upper",   "FID:UPPER"},
        {"lower",   "FID:LOWER"},
        {"reverse", "FID:REVERSE"},
        {"ascii", "FID:ASCII"},
        {"hash",  "FID:HASH"},
        {"id",    "FID:ID"},
        {"repr",  "FID:REPR"},
        {"help",  "FID:HELP"},
        {"pow",   "FID:POW"},
        {"sum",   "FID:SUM"},
        {"strip",   "FID:STRIP"},
        {"find",    "FID:FIND"},
        {"replace", "FID:REPLACE"},
    };

    auto it = BUILTINS.find(fn);
    if (it == BUILTINS.end()) {
        // 用户函数：参数按逗号切分（跟踪括号深度），再 encode_arg
        std::vector<std::string> args;
        std::string cur;
        bool in_str = false;
        int paren = 0;
        for (char c : arg) {
            if (c == '"') in_str = !in_str;
            if (!in_str) {
                if (c == '(') paren++;
                else if (c == ')') paren--;
            }
            if (c == ',' && !in_str && paren == 0) {
                args.push_back(trim(cur)); cur.clear();
            } else cur += c;
        }
        if (!cur.empty()) args.push_back(trim(cur));

        std::string out = "FID:CALL NAME:SID:22[" + fn + "]22 ARGS:";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) out += ",";
            out += encode_arg(args[i]);   // 🚀 统一入口
        }
        return out;
    }

    if (arg.size() >= 2 && (arg.front() == '"' || arg.front() == '\'')) {
        std::string v = arg.substr(1, arg.size() - 2);
        v = process_escapes(v);
        if (fn == "len") return "NID:" + std::to_string(v.size());
        if (fn == "ord") return "NID:" + std::to_string(v.empty() ? 0 : (int)(unsigned char)v[0]);
        if (fn == "str") return "SID:22[" + v + "]22";
    }

    {
        std::vector<std::string> args;
        std::string cur;
        bool in_str = false;
        int paren = 0;
        for (char c : arg) {
            if (c == '"') in_str = !in_str;
            if (!in_str) {
                if (c == '(') paren++;
                else if (c == ')') paren--;
            }
            if (c == ',' && !in_str && paren == 0) {
                args.push_back(trim(cur)); cur.clear();
            } else cur += c;
        }
        if (!cur.empty()) args.push_back(trim(cur));

        if (args.size() >= 2) {
            std::string out = it->second;
            for (const auto& a : args) out += " " + encode_arg(a);   // 🚀 统一入口
            return out;
        }
        if (args.size() == 1) {
            return it->second + " " + encode_arg(args[0]);           // 🚀 统一入口
        }
    }

    return it->second + " " + encode_arg(arg);                       // 🚀 统一入口
}

// ============================================================
// 切片识别器
// ============================================================
std::string try_slice(const std::string& s) {
    if (s.size() < 3 || s.back() != ']') return "";
    size_t lb = s.rfind('[');
    if (lb == std::string::npos || lb == 0) return "";

    std::string var_name = trim(s.substr(0, lb));
    std::string idx_str  = trim(s.substr(lb + 1, s.size() - lb - 2));

    if (var_name.empty() || idx_str.empty()) return "";

    for (char c : var_name)
        if (!isalnum((unsigned char)c) && c != '_') return "";

    size_t start = (idx_str[0] == '-') ? 1 : 0;
    if (start >= idx_str.size()) return "";
    for (size_t i = start; i < idx_str.size(); ++i)
        if (!isdigit((unsigned char)idx_str[i])) return "";

    return "SLICE:" + var_name + ":" + idx_str;
}

std::string parseValue(const std::string& right) {
    std::string val = trim(right);

    if (val.rfind("f\"", 0) == 0) {
        std::string inner = val.substr(2, val.size() - 3);
        return parse_template_string(inner);
    }

    if (val.size() >= 2 && val.front() == '[' && val.back() == ']') {
        std::string inner = val.substr(1, val.size() - 2);
        std::vector<std::string> elems;
        std::string cur;
        bool in_str = false;
        for (char c : inner) {
            if (c == '"') in_str = !in_str;
            if (c == ',' && !in_str) {
                elems.push_back(trim(cur));
                cur.clear();
            } else cur += c;
        }
        if (!cur.empty() || !elems.empty()) elems.push_back(trim(cur));

        std::string out = "LST:";
        for (size_t i = 0; i < elems.size(); ++i) {
            if (i > 0) out += '\x01';
            out += parseValue(elems[i]);
        }
        return out;
    }

    std::string val_no_space = val;
    val_no_space.erase(std::remove_if(val_no_space.begin(), val_no_space.end(), ::isspace), val_no_space.end());

    if (val_no_space.rfind("input(", 0) == 0 ||
        val_no_space.rfind("int(input(", 0) == 0 ||
        val_no_space.rfind("float(input(", 0) == 0 ||
        val_no_space.rfind("string(input(", 0) == 0) {

        std::string type = "SID";
        if (val_no_space.rfind("int(input(", 0) == 0) type = "NID";
        else if (val_no_space.rfind("float(input(", 0) == 0) type = "FLT";
        else if (val_no_space.rfind("string(input(", 0) == 0) type = "SID";

        std::string prompt = "";
        size_t p_start = val.find("input(");
        if (p_start != std::string::npos) {
            p_start += 6;
            size_t p_end = val.find(")", p_start);
            if (p_end != std::string::npos) {
                std::string inner = trim(val.substr(p_start, p_end - p_start));
                if (inner.size() >= 2 && inner.front() == '"' && inner.back() == '"') {
                    inner = inner.substr(1, inner.size() - 2);
                }
                inner = process_escapes(inner);
                prompt = inner;
            }
        }
        return "FID:INPUT TYPE:" + type + " PROMPT:SID:22[" + prompt + "]22";
    }

    if (val_no_space.rfind("len(", 0) == 0 && val_no_space.back() == ')') {
        size_t lp = val_no_space.find('(');
        size_t rp = val_no_space.rfind(')');
        if (rp > lp) {
            std::string arg = trim(val_no_space.substr(lp + 1, rp - lp - 1));
            if (arg.size() >= 2 && (arg.front() == '"' || arg.front() == '\'')) {
                std::string v = arg.substr(1, arg.size() - 2);
                v = process_escapes(v);
                return "NID:" + std::to_string(v.size());
            }
            return "FID:LEN VID:" + arg;
        }
    }

    if (val.rfind("float(", 0) == 0 && val.back() == ')') {
        std::string inner = trim(val.substr(6, val.size() - 7));
        int dotCount = 0; bool valid = !inner.empty();
        for (char c : inner) {
            if (c == '.') dotCount++;
            else if (!isdigit(c)) { valid = false; break; }
        }
        if (!valid || dotCount > 1) {
            std::cerr << "\n[编译错误] 浮点数类型只能包含数字和小数点: float(" << inner << ")\n";
            exit(1);
        }
        if (dotCount == 0) inner += ".0";
        return "FLT:" + inner;
    }
    if (val.rfind("int(", 0) == 0 && val.back() == ')') {
        std::string inner = trim(val.substr(4, val.size() - 5));
        if (!inner.empty() && isdigit((unsigned char)inner[0])) {
            while (inner.length() < 4) inner = "0" + inner;
            return "NID:" + inner;
        }
        return "FID:INT VID:" + inner;
    }
    if (val.rfind("string(", 0) == 0 && val.back() == ')') {
        std::string inner = trim(val.substr(7, val.size() - 8));
        if (inner.size() >= 2 && inner.front() == '"' && inner.back() == '"') inner = inner.substr(1, inner.size() - 2);
        inner = process_escapes(inner);
        return "SID:22[" + inner + "]22";
    }

    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
        std::string inner = val.substr(1, val.size() - 2);
        inner = process_escapes(inner);
        if (inner.find("${") != std::string::npos) {
            return parse_template_string(inner);
        }
        return "SID:22[" + inner + "]22";
    }
    bool hasLetter = false;
    for (char c : val) if (isalpha(c)) hasLetter = true;
    if (!hasLetter && val.find('.') != std::string::npos) return "FLT:" + val;
    if (!val.empty() && isdigit(val[0])) {
        while (val.length() < 4) val = "0" + val;
        return "NID:" + val;
    }
    return "VID:" + val;
}

// ============================================================
// 表达式解析：按优先级递归
// ============================================================
std::string parse_expression(const std::string& expr) {
    std::string s = trim(expr);
    if (s.empty()) return "";

    {
        std::string b = try_builtin_call(s);
        if (!b.empty()) return b;
    }
    {
        std::string sl = try_slice(s);
        if (!sl.empty()) return sl;
    }

    // 比较
    {
        bool in_string = false;
        int paren = 0;
        for (int i = (int)s.size() - 1; i >= 0; --i) {
            if (s[i] == '"') in_string = !in_string;
            if (in_string) continue;
            if (s[i] == ')') paren++;
            if (s[i] == '(') paren--;
            if (paren > 0) continue;

            if (i > 0) {
                std::string two = s.substr(i - 1, 2);
                std::string op;
                if      (two == ">=") op = "[GE]";
                else if (two == "<=") op = "[LE]";
                else if (two == "==") op = "[EQ]";
                else if (two == "!=") op = "[NE]";
                if (!op.empty()) {
                    std::string left  = parse_expression(s.substr(0, i - 1));
                    std::string right = parse_expression(s.substr(i + 1));
                    return left + op + right;
                }
            }
            if (s[i] == '>') {
                std::string left  = parse_expression(s.substr(0, i));
                std::string right = parse_expression(s.substr(i + 1));
                return left + "[GT]" + right;
            }
            if (s[i] == '<') {
                std::string left  = parse_expression(s.substr(0, i));
                std::string right = parse_expression(s.substr(i + 1));
                return left + "[LT]" + right;
            }
        }
    }

    // 加减
    {
        bool in_string = false;
        int paren = 0;
        for (int i = s.size() - 1; i >= 0; --i) {
            if (s[i] == '"') in_string = !in_string;
            if (in_string) continue;
            if (s[i] == ')') paren++;
            if (s[i] == '(') paren--;
            if (paren > 0) continue;
            if (s[i] == '+' || s[i] == '-') {
                std::string left = parse_expression(s.substr(0, i));
                std::string right = parse_expression(s.substr(i + 1));
                std::string op = (s[i] == '+') ? "[ADD]" : "[SUB]";
                return left + op + right;
            }
        }
    }

    // 乘除
    {
        bool in_string = false;
        int paren = 0;
        for (int i = s.size() - 1; i >= 0; --i) {
            if (s[i] == '"') in_string = !in_string;
            if (in_string) continue;
            if (s[i] == ')') paren++;
            if (s[i] == '(') paren--;
            if (paren > 0) continue;

            if (s[i] == '*' && i > 0 && s[i-1] == '*') continue;
            if (s[i] == '*' && i + 1 < (int)s.size() && s[i+1] == '*') continue;

            if (s[i] == '/' && i > 0 && s[i-1] == '/') {
                std::string left  = parse_expression(s.substr(0, i - 1));
                std::string right = parse_expression(s.substr(i + 1));
                return left + "[IDIV]" + right;
            }
            if (s[i] == '/' && i + 1 < (int)s.size() && s[i+1] == '/') continue;

            if (s[i] == '*') {
                std::string left  = parse_expression(s.substr(0, i));
                std::string right = parse_expression(s.substr(i + 1));
                return left + "[MUL]" + right;
            }
            if (s[i] == '/') {
                std::string left  = parse_expression(s.substr(0, i));
                std::string right = parse_expression(s.substr(i + 1));
                return left + "[DIV]" + right;
            }
        }
    }

    // 幂
    {
        bool in_string = false;
        int paren = 0;
        for (int i = s.size() - 2; i >= 0; --i) {
            if (s[i] == '"') in_string = !in_string;
            if (in_string) continue;
            if (s[i] == ')') paren++;
            if (s[i] == '(') paren--;
            if (paren > 0) continue;
            if (s[i] == '*' && s[i+1] == '*') {
                std::string left  = parse_expression(s.substr(0, i));
                std::string right = parse_expression(s.substr(i + 2));
                return left + "[POW]" + right;
            }
        }
    }

    // 整体括号 → {G ...}
    if (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
        int d = 0;
        bool whole = true;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '(') d++;
            else if (s[i] == ')') {
                d--;
                if (d == 0 && i != s.size() - 1) { whole = false; break; }
            }
        }
        if (whole) {
            return "{G " + parse_expression(s.substr(1, s.size() - 2)) + "}";
        }
    }

    return parseValue(s);
}