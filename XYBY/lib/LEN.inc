// ============================================================
// LEN.inc —— FID:LEN 求长度
// ============================================================
bool isLenExpression(const std::string& s) {
    if (s.rfind("FID:LEN", 0) != 0) return false;
    return true;
}

std::string evalLenExpression(const std::string& s, MemorySpace& mem) {
    // 从 AST 里取变量名：FID:LEN VID:xxx
    size_t v = s.find("VID:");
    if (v == std::string::npos) return "0";

    size_t start = v + 4;
    size_t end = start;
    while (end < s.size() &&
           (isalnum((unsigned char)s[end]) || s[end] == '_')) end++;
    std::string var_name = s.substr(start, end - start);

    // 取值
    std::string val = resolve_deep_value(var_name, mem);

    // 返回长度
    return std::to_string(val.size());
}