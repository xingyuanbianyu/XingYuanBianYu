// ============================================================
// TMPL: 模板求值
// 扫描 SID:22[...]22 和 VID:xxx，把变量替换成实际值
// ============================================================
std::string eval_template(const std::string& s, MemorySpace& mem) {
    // s 应该是 "TMPL:..." 形式
    if (s.size() < 5 || s.rfind("TMPL:", 0) != 0) return s;
    std::string rest = s.substr(5);

    std::string out;
    size_t pos = 0;
    while (pos < rest.size()) {
        // --- 字符串字面量 SID:22[...]22 ---
        if (rest.compare(pos, 7, "SID:22[") == 0) {
            size_t e = rest.find("]22", pos + 7);
            if (e == std::string::npos) break;
            out += rest.substr(pos + 7, e - pos - 7);
            pos = e + 3;
        }
        // --- 变量引用 VID:xxx ---
        else if (rest.compare(pos, 4, "VID:") == 0) {
            size_t vs = pos + 4;
            size_t ve = vs;
            while (ve < rest.size()) {
                // 遇到下一个标记就停
                if (rest.compare(ve, 4, "SID:") == 0 ||
                    rest.compare(ve, 4, "VID:") == 0 ||
                    rest.compare(ve, 4, "NID:") == 0 ||
                    rest.compare(ve, 4, "FLT:") == 0) {
                    break;
                }
                if (isalnum((unsigned char)rest[ve]) || rest[ve] == '_') {
                    ve++;
                } else {
                    break;
                }
            }
            std::string var = rest.substr(vs, ve - vs);
            out += resolve_deep_value(var, mem);
            pos = ve;
        }
        // --- 其它字符原样保留 ---
        else {
            out += rest[pos];
            pos++;
        }
    }
    return out;
}