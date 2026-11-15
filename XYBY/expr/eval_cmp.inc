// ============================================================
// 比较运算（支持两种形式）
//   源码形式：== != >= <= > <
//   AST 标记： [EQ] [NE] [GE] [LE] [GT] [LT]
// 返回 nullopt 表示没有比较运算符，交回上层
// ============================================================
std::optional<std::string> try_eval_comparison(const std::string& s, MemorySpace& mem) {
    int last_cmp = -1;
    std::string cmp_op;
    int paren_level = 0;

    for (int i = 0; i < (int)s.size(); ++i) {
        if (s[i] == '(') paren_level++;
        if (s[i] == ')') paren_level--;
        if (paren_level != 0) continue;

        // ---------- AST 标记形式（4 字符）----------
        if (s.compare(i, 4, "[GE]") == 0) { last_cmp = i; cmp_op = "[GE]"; i += 3; continue; }
        if (s.compare(i, 4, "[LE]") == 0) { last_cmp = i; cmp_op = "[LE]"; i += 3; continue; }
        if (s.compare(i, 4, "[EQ]") == 0) { last_cmp = i; cmp_op = "[EQ]"; i += 3; continue; }
        if (s.compare(i, 4, "[NE]") == 0) { last_cmp = i; cmp_op = "[NE]"; i += 3; continue; }
        if (s.compare(i, 4, "[GT]") == 0) { last_cmp = i; cmp_op = "[GT]"; i += 3; continue; }
        if (s.compare(i, 4, "[LT]") == 0) { last_cmp = i; cmp_op = "[LT]"; i += 3; continue; }

        // ---------- 源码形式 ----------
        if (s.compare(i, 2, "==") == 0) { last_cmp = i; cmp_op = "=="; i += 1; continue; }
        if (s.compare(i, 2, "!=") == 0) { last_cmp = i; cmp_op = "!="; i += 1; continue; }
        if (s.compare(i, 2, ">=") == 0) { last_cmp = i; cmp_op = ">="; i += 1; continue; }
        if (s.compare(i, 2, "<=") == 0) { last_cmp = i; cmp_op = "<="; i += 1; continue; }
        if (s[i] == '>') { last_cmp = i; cmp_op = ">"; continue; }
        if (s[i] == '<') { last_cmp = i; cmp_op = "<"; continue; }
    }

    if (last_cmp == -1) return std::nullopt;

    std::string left_val  = eval_expr(s.substr(0, last_cmp), mem);
    std::string right_val = eval_expr(s.substr(last_cmp + cmp_op.size()), mem);

    // 归一化：把源码运算符映射成 AST 标记，统一比较
    std::string norm = cmp_op;
    if      (cmp_op == "==") norm = "[EQ]";
    else if (cmp_op == "!=") norm = "[NE]";
    else if (cmp_op == ">=") norm = "[GE]";
    else if (cmp_op == "<=") norm = "[LE]";
    else if (cmp_op == ">")  norm = "[GT]";
    else if (cmp_op == "<")  norm = "[LT]";

    bool result = false;
    try {
        double dl = std::stod(left_val);
        double dr = std::stod(right_val);
        if      (norm == "[EQ]") result = (dl == dr);
        else if (norm == "[NE]") result = (dl != dr);
        else if (norm == "[GT]") result = (dl >  dr);
        else if (norm == "[LT]") result = (dl <  dr);
        else if (norm == "[GE]") result = (dl >= dr);
        else if (norm == "[LE]") result = (dl <= dr);
    } catch (...) {
        if      (norm == "[EQ]") result = (left_val == right_val);
        else if (norm == "[NE]") result = (left_val != right_val);
        else if (norm == "[GT]") result = (left_val >  right_val);
        else if (norm == "[LT]") result = (left_val <  right_val);
        else if (norm == "[GE]") result = (left_val >= right_val);
        else if (norm == "[LE]") result = (left_val <= right_val);
    }
    return result ? "True" : "False";
}
// ============================================================
// 🚀 逻辑运算符求值（优先级低于比较，先拆）
//   返回 "True" / "False"，与 try_eval_comparison 保持一致
// ============================================================

// 真值判定：兼容 "True"/"False"/数字/空串
static bool xy_is_truthy(const std::string& v) {
    if (v.empty()) return false;
    if (v == "False" || v == "false" || v == "None" || v == "none") return false;
    if (v == "True"  || v == "true")  return true;
    try { return std::stod(v) != 0; } catch (...) { return true; }
}

// 找“最后一个”顶层（跳过括号）标记位置
static size_t xy_find_top_level_last(const std::string& expr, const std::string& tag) {
    int depth = 0;
    size_t last = std::string::npos;
    for (size_t i = 0; i + tag.size() <= expr.size(); ++i) {
        if (expr[i] == '(') depth++;
        else if (expr[i] == ')') depth--;
        else if (depth == 0 && expr.compare(i, tag.size(), tag) == 0) {
            last = i;
            i += tag.size() - 1;
        }
    }
    return last;
}

// [OR] 中缀：VID:a[OR]VID:b
std::optional<std::string> try_eval_or(const std::string& s, MemorySpace& mem) {
    size_t pos = xy_find_top_level_last(s, "[OR]");
    if (pos == std::string::npos) return std::nullopt;

    std::string left  = eval_expr(s.substr(0, pos), mem);
    std::string right = eval_expr(s.substr(pos + 4), mem);

    bool r = xy_is_truthy(left) || xy_is_truthy(right);
    return r ? "True" : "False";
}

// [AND] 中缀：VID:a[AND]VID:b
std::optional<std::string> try_eval_and(const std::string& s, MemorySpace& mem) {
    size_t pos = xy_find_top_level_last(s, "[AND]");
    if (pos == std::string::npos) return std::nullopt;

    std::string left  = eval_expr(s.substr(0, pos), mem);
    std::string right = eval_expr(s.substr(pos + 5), mem);

    bool r = xy_is_truthy(left) && xy_is_truthy(right);
    return r ? "True" : "False";
}

// [NOT] 前缀：[NOT]VID:b
std::optional<std::string> try_eval_not(const std::string& s, MemorySpace& mem) {
    if (s.rfind("[NOT]", 0) != 0) return std::nullopt;

    std::string operand = eval_expr(trim(s.substr(5)), mem);
    bool r = !xy_is_truthy(operand);
    return r ? "True" : "False";
}