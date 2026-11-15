// ============================================================
// IF.inc —— KID:IF
// 只保留 handleIf，handleElse / handleElif 分别在 ELSE.inc / ELIF.inc
// ============================================================
bool handleIf(const std::string& line, MemorySpace& mem,
              std::map<int, bool>& if_passed,
              int brace_level, int& skip_level,
              const std::vector<std::string>& lines, size_t line_idx) {
    if (line.find("KID:IF") == std::string::npos) return false;

    // 【修复】不再 push 空作用域 —— IF 是控制流分支，不是作用域

    bool passed = false;
    std::string cond_node;
    size_t cond_pos = line.find("COND:");
    if (cond_pos != std::string::npos) {
        size_t end_pos = line.find("}MEID:", cond_pos);
        if (end_pos != std::string::npos)
            cond_node = line.substr(cond_pos + 5, end_pos - cond_pos - 5);
    }

    // 🚀 【新增】优先拦截 TRF 节点，直接丢给 eval_expr 去执行
    if (cond_node.rfind("{KID:TRF", 0) == 0) {
        std::string result = eval_expr(cond_node, mem);
        // 兼容各种真值返回格式
        passed = (result == "True" || result == "true" || result == "1" || result == "TRUE");
    }
    else if (cond_node.rfind("RAW:", 0) == 0) {
        std::string raw_cond = cond_node.substr(4);
        std::string clean_cond;
        for (char c : raw_cond) {
            if (c != ' ') clean_cond += c;
        }
        std::string result = eval_expr(clean_cond, mem);
        passed = (result == "True" || result == "true" || result == "1");
    } 
    else {
        passed = evaluateCondition(line, mem);
    }

    if_passed[brace_level] = passed;
    
    bool has_sibling = hasSiblingBranch(lines, line_idx, brace_level);
    if (!passed && has_sibling) {
        skip_level = brace_level + 1;
    } else {
        skip_level = -1;
    }
    return true;
}