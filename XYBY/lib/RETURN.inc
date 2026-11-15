bool handleReturn(const std::string& line, MemorySpace& mem) {
    if (line.find("KID:RETURN") == std::string::npos) return false;

    std::string expr = line.substr(line.find("KID:RETURN") + 10);
    size_t end = expr.find("}}MEID:");
    if (end != std::string::npos) expr = expr.substr(0, end);
    else {
        end = expr.find("}MEID:");
        if (end != std::string::npos) expr = expr.substr(0, end);
        else {
            end = expr.find("}");
            if (end != std::string::npos) expr = expr.substr(0, end);
        }
    }
    expr = trim(expr);

    std::string val = expr.empty() ? "None" : eval_expr(expr, mem);

    if (val.rfind("LST:", 0) == 0) {
        std::string cur, evaluated = "LST:";
        bool first = true;
        for (size_t k = 4; k <= val.size(); ++k) {
            if (k == val.size() || val[k] == '\x01') {
                if (!cur.empty()) {
                    std::string v = eval_expr(cur, mem);
                    if (!first) evaluated += '\x01';
                    evaluated += v;
                    first = false;
                }
                cur.clear();
            } else cur += val[k];
        }
        val = evaluated;
    }

    mem.last_return_value = val;
    mem.has_returned = true; // 🚀 核心修复：标记已返回
    g_last_xy_return_value = val; 

    throw ReturnSignal{val};
}