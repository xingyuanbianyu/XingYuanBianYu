// ============================================================
// ABS.inc —— FID:ABS 求绝对值
// AST 格式：FID:ABS VID:xxx
// ============================================================
bool isAbsExpression(const std::string& s) {
    return s.rfind("FID:ABS", 0) == 0;
}

std::string evalAbsExpression(const std::string& s, MemorySpace& mem) {
    size_t v = s.find("VID:");
    if (v == std::string::npos) return "0";

    size_t start = v + 4;
    size_t end = start;
    while (end < s.size() &&
           (isalnum((unsigned char)s[end]) || s[end] == '_')) end++;
    std::string var_name = s.substr(start, end - start);

    std::string val = resolve_deep_value(var_name, mem);

    // 试着当浮点数处理
    try {
        double d = std::stod(val);
        if (d < 0) d = -d;
        return format_double(d);
    } catch (...) {
        return val;
    }
}