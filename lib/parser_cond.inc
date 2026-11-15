// =================================================================
// parser_cond.inc —— 条件逻辑解析
// =================================================================
std::vector<std::string> split_top_or(const std::string& s) {
    std::vector<std::string> result;
    bool in_str = false;
    int paren = 0;
    size_t last = 0;
    for (size_t i = 0; i + 4 <= s.size(); ++i) {
        if (s[i] == '"') in_str = !in_str;
        if (in_str) continue;
        if (s[i] == '(') paren++;
        if (s[i] == ')') paren--;
        if (paren > 0) continue;
        if (s.compare(i, 4, " or ") == 0) {
            result.push_back(trim(s.substr(last, i - last)));
            last = i + 4;
            i += 3;
        }
    }
    result.push_back(trim(s.substr(last)));
    return result;
}

std::vector<std::string> split_top_and(const std::string& s) {
    std::vector<std::string> result;
    bool in_str = false;
    int paren = 0;
    size_t last = 0;
    for (size_t i = 0; i + 5 <= s.size(); ++i) {
        if (s[i] == '"') in_str = !in_str;
        if (in_str) continue;
        if (s[i] == '(') paren++;
        if (s[i] == ')') paren--;
        if (paren > 0) continue;
        if (s.compare(i, 5, " and ") == 0) {
            result.push_back(trim(s.substr(last, i - last)));
            last = i + 5;
            i += 4;
        }
    }
    result.push_back(trim(s.substr(last)));
    return result;
}

std::string single_cmp_to_ast(const std::string& cond) {
    struct CmpOp { const char* sym; const char* tag; };
    static const CmpOp cmps[] = {
        {"==>", "[MATCH_R]"},
        {"<==", "[MATCH_L]"},
        {">=",  "[GE]"},
        {"<=",  "[LE]"},
        {"!=",  "[NE]"},
        {"==",  "[EQ]"},
        {">",   "[GT]"},
        {"<",   "[LT]"},
    };

    bool in_str = false;
    for (size_t k = 0; k < cond.size(); ++k) {
        if (cond[k] == '"') in_str = !in_str;
        if (in_str) continue;
        for (const auto& c : cmps) {
            if (cond.compare(k, strlen(c.sym), c.sym) == 0) {
                std::string left  = trim(cond.substr(0, k));
                std::string right = trim(cond.substr(k + strlen(c.sym)));
                if (left.empty() || right.empty()) continue;
                return "VID:" + left + c.tag + parse_expression(right);
            }
        }
    }
    return "";
}

std::string cond_to_ast(const std::string& cond) {
    std::string c = trim(cond);

    while (c.size() >= 2 && c.front() == '(' && c.back() == ')') {
        int d = 0; bool wrap = true;
        for (size_t i = 0; i < c.size(); ++i) {
            if (c[i] == '(') d++;
            else if (c[i] == ')') {
                d--;
                if (d == 0 && i != c.size() - 1) { wrap = false; break; }
            }
        }
        if (!wrap) break;
        c = trim(c.substr(1, c.size() - 2));
    }

    if (c == "True")  return "BOOL:TRUE";
    if (c == "False") return "BOOL:FALSE";
    if (c == "None")  return "BOOL:NONE";

    if (c.rfind("not ", 0) == 0) {
        std::string inner = cond_to_ast(c.substr(4));
        if (inner.empty()) return "";
        return "[NOT]" + inner;
    }

    auto or_parts = split_top_or(c);
    if (or_parts.size() > 1) {
        std::string result;
        for (size_t i = 0; i < or_parts.size(); ++i) {
            if (i > 0) result += "[OR]";
            std::string sub = cond_to_ast(or_parts[i]);
            if (sub.empty()) return "";
            result += sub;
        }
        return result;
    }

    auto and_parts = split_top_and(c);
    if (and_parts.size() > 1) {
        std::string result;
        for (size_t i = 0; i < and_parts.size(); ++i) {
            if (i > 0) result += "[AND]";
            std::string sub = cond_to_ast(and_parts[i]);
            if (sub.empty()) return "";
            result += sub;
        }
        return result;
    }

    return single_cmp_to_ast(c);
}