// ============================================================
// if / elif 判断
// ============================================================
bool evaluateCondition(const std::string& line, MemorySpace& mem) {
    if (line.find("BOOL:TRUE")  != std::string::npos) return true;
    if (line.find("BOOL:FALSE") != std::string::npos) return false;
    if (line.find("BOOL:NONE")  != std::string::npos) return false;

    std::string expr = extract_condition_expr(line);
    if (expr.empty()) return false;

    return eval_bool_expr(expr, mem, /*use_deep=*/false);
}