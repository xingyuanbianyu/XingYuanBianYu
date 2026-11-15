// ============================================================
// 从 IF / ELIF / WHILE 的 AST 行里提取条件表达式
// 适配 AST 格式：KID:IF COND:xxx}MEID:LID:N
// ============================================================
std::string extract_condition_expr(const std::string& line) {
    const char* keys[] = {"KID:IF", "KID:ELIF", "KID:WHILE"};
    for (const char* key : keys) {
        size_t pos = line.find(key);
        if (pos == std::string::npos) continue;

        size_t p = pos + std::strlen(key);
        // 防止 KID:IFOO 之类的误匹配
        if (p < line.size() && (isalnum((unsigned char)line[p]) || line[p] == '_')) continue;

        size_t cond_pos = line.find("COND:", p);
        if (cond_pos == std::string::npos) continue;

        size_t start = cond_pos + 5;
        size_t end = line.find("}MEID:", start);
        if (end == std::string::npos) end = line.rfind("}");
        if (end > start) return line.substr(start, end - start);
    }
    return "";
}