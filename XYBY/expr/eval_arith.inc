// ============================================================
// 算术运算：[ADD] [SUB] [MUL] [DIV] [IDIV] [POW]
//   支持 {G ...} 分组标记（保留括号优先级）
//   支持 FID:CALL ... ARGS:xxx —— ARGS 内的运算符不算顶层
// ============================================================
std::optional<std::string> try_eval_arithmetic(const std::string& s, MemorySpace& mem) {
    // ============================================================
    // 🚀 最外层包裹检测：如果整串就是 {G ...}，递归求值内部
    // ============================================================
    if (s.size() >= 4 && s.compare(0, 3, "{G ") == 0 && s.back() == '}') {
        int d = 0;
        bool whole_group = true;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '{') d++;
            else if (s[i] == '}') {
                d--;
                if (d == 0 && i != s.size() - 1) { whole_group = false; break; }
            }
        }
        if (whole_group) {
            std::string inner = trim(s.substr(3, s.size() - 4));
            return eval_expr(inner, mem);
        }
    }

    // ============================================================
    // 顶层扫描：找最右的运算符。
    //   { } 和 ( ) 算深度；FID:CALL 的 ARGS 段整体跳过。
    // ============================================================
    int last_add_sub = -1;
    int last_mul_div = -1;
    int last_idiv    = -1;
    int last_pow     = -1;
    int paren_level  = 0;

    for (int i = 0; i < (int)s.size(); ++i) {
        // ============================================================
        // 🚀 遇到 FID:CALL 就整体跳过（包括它的 NAME / ARGS）
        //    防止 ARGS 内部的 [SUB]/[ADD] 被当成顶层运算符
        // ============================================================
        if (s.compare(i, 9, "FID:CALL ") == 0) {
            size_t args_pos = s.find("ARGS:", i);
            if (args_pos != std::string::npos) {
                size_t j = args_pos + 5;
                int inner = 0;
                while (j < s.size()) {
                    char c = s[j];
                    if (c == '(' || c == '{') { inner++; j++; continue; }
                    if (c == ')' || c == '}') {
                        if (inner == 0) break;
                        inner--;
                        j++;
                        continue;
                    }
                    if (inner == 0) {
                        if (s.compare(j, 6, "[IDIV]") == 0 ||
                            s.compare(j, 5, "[ADD]")  == 0 ||
                            s.compare(j, 5, "[SUB]")  == 0 ||
                            s.compare(j, 5, "[MUL]")  == 0 ||
                            s.compare(j, 5, "[DIV]")  == 0 ||
                            s.compare(j, 5, "[POW]")  == 0 ||
                            s.compare(j, 5, "[GT]")   == 0 ||
                            s.compare(j, 5, "[LT]")   == 0 ||
                            s.compare(j, 5, "[EQ]")   == 0 ||
                            s.compare(j, 5, "[NE]")   == 0 ||
                            s.compare(j, 5, "[GE]")   == 0 ||
                            s.compare(j, 5, "[LE]")   == 0 ||
                            s.compare(j, 5, "[OR]")   == 0 ||
                            s.compare(j, 6, "[AND]")  == 0 ||
                            s.compare(j, 5, "[NOT]")  == 0) {
                            break;
                        }
                        if (s.compare(j, 9, "FID:CALL ") == 0) break;
                    }
                    j++;
                }
                // 🚀 修复：跳过 ARGS 段（i 要正好停在 j 上，让 ++i 后从 j+1 开始）
                i = (int)j;
                continue;
            }
        }

        if (s[i] == '(' || s[i] == '{') paren_level++;
        if (s[i] == ')' || s[i] == '}') paren_level--;
        if (paren_level != 0) continue;

        if (s.compare(i, 6, "[IDIV]") == 0) { last_idiv    = i; i += 5; continue; }
        if (s.compare(i, 5, "[ADD]")  == 0) { last_add_sub = i; i += 4; continue; }
        if (s.compare(i, 5, "[SUB]")  == 0) { last_add_sub = i; i += 4; continue; }
        if (s.compare(i, 5, "[MUL]")  == 0) { last_mul_div = i; i += 4; continue; }
        if (s.compare(i, 5, "[DIV]")  == 0) { last_mul_div = i; i += 4; continue; }
        if (s.compare(i, 5, "[POW]")  == 0) { last_pow     = i; i += 4; continue; }
    }

    // 含 FID:CALL 但无顶层算术标记 → 交给 FID:CALL 分支
    if (last_add_sub == -1 && last_idiv == -1 &&
        last_mul_div == -1 && last_pow == -1 &&
        s.find("NAME:SID:") != std::string::npos) {
        return std::nullopt;
    }

    // ============================================================
    // 递归拆分
    // ============================================================
    if (last_add_sub != -1) {
        std::string left  = eval_expr(s.substr(0, last_add_sub), mem);
        std::string right = eval_expr(s.substr(last_add_sub + 5), mem);
        return apply_math_op(left, s.substr(last_add_sub, 5), right);
    }
    if (last_idiv != -1) {
        std::string left  = eval_expr(s.substr(0, last_idiv), mem);
        std::string right = eval_expr(s.substr(last_idiv + 6), mem);
        return apply_math_op(left, "[IDIV]", right);
    }
    if (last_mul_div != -1) {
        std::string left  = eval_expr(s.substr(0, last_mul_div), mem);
        std::string right = eval_expr(s.substr(last_mul_div + 5), mem);
        return apply_math_op(left, s.substr(last_mul_div, 5), right);
    }
    if (last_pow != -1) {
        std::string left  = eval_expr(s.substr(0, last_pow), mem);
        std::string right = eval_expr(s.substr(last_pow + 5), mem);
        return apply_math_op(left, "[POW]", right);
    }

    // ============================================================
    // 裸符号兜底扫描（同时处理 { } 分组和 FID:CALL 跳过）
    // ============================================================
    {
        int depth = 0;
        bool in_str = false;
        bool in_bracket = false;
        int bare_add_sub = -1;
        int bare_mul_div = -1;

        for (int i = 0; i < (int)s.size(); ++i) {
            char c = s[i];

            if (s.compare(i, 9, "FID:CALL ") == 0) {
                size_t args_pos = s.find("ARGS:", i);
                if (args_pos != std::string::npos) {
                    size_t j = args_pos + 5;
                    int inner = 0;
                    while (j < s.size()) {
                        char cj = s[j];
                        if (cj == '(' || cj == '{') { inner++; j++; continue; }
                        if (cj == ')' || cj == '}') {
                            if (inner == 0) break;
                            inner--;
                            j++;
                            continue;
                        }
                        if (inner == 0) {
                            if (s.compare(j, 6, "[IDIV]") == 0 ||
                                s.compare(j, 5, "[ADD]")  == 0 ||
                                s.compare(j, 5, "[SUB]")  == 0 ||
                                s.compare(j, 5, "[MUL]")  == 0 ||
                                s.compare(j, 5, "[DIV]")  == 0 ||
                                s.compare(j, 5, "[POW]")  == 0) {
                                break;
                            }
                            if (s.compare(j, 9, "FID:CALL ") == 0) break;
                        }
                        j++;
                    }
                    // 🚀 同样修复
                    i = (int)j;
                    continue;
                }
            }

            if (c == '"') { in_str = !in_str; continue; }
            if (in_str) continue;
            if (c == '[') { in_bracket = true; continue; }
            if (c == ']') { in_bracket = false; continue; }
            if (in_bracket) continue;
            if (c == '(' || c == '{') { depth++; continue; }
            if (c == ')' || c == '}') { depth--; continue; }
            if (depth != 0) continue;

            if (c == '+' || c == '-') {
                if (i == 0) continue;
                int j = i - 1;
                while (j >= 0 && s[j] == ' ') j--;
                if (j < 0) continue;
                char prev = s[j];
                if (prev == '+' || prev == '-' || prev == '*' || prev == '/' ||
                    prev == '(' || prev == ',' || prev == '{') continue;
                bare_add_sub = i;
            } else if (c == '*' || c == '/') {
                if (bare_mul_div == -1) bare_mul_div = i;
            }
        }

        if (bare_add_sub != -1) {
            std::string left  = eval_expr(s.substr(0, bare_add_sub), mem);
            std::string right = eval_expr(s.substr(bare_add_sub + 1), mem);
            std::string tag   = (s[bare_add_sub] == '+') ? "[ADD]" : "[SUB]";
            return apply_math_op(left, tag, right);
        }
        if (bare_mul_div != -1) {
            std::string left  = eval_expr(s.substr(0, bare_mul_div), mem);
            std::string right = eval_expr(s.substr(bare_mul_div + 1), mem);
            std::string tag   = (s[bare_mul_div] == '*') ? "[MUL]" : "[DIV]";
            return apply_math_op(left, tag, right);
        }
    }

    return std::nullopt;
}