// ============================================================
// 表达式模块通用小工具
// ============================================================
std::string peek_var_after(const std::string& s, size_t vid_pos, size_t* end_pos = nullptr) {
    if (vid_pos == std::string::npos) return "";
    size_t s0 = vid_pos + 4, e0 = s0;
    // 【改动】放行 '.'，让 self.name / obj.field 完整识别
    while (e0 < s.size() && (isalnum((unsigned char)s[e0]) || s[e0] == '_' || s[e0] == '.')) e0++;
    if (end_pos) *end_pos = e0;
    return s.substr(s0, e0 - s0);
}

// 取 "NAME:SID:22[fname]22" 里的 fname
std::string peek_call_name(const std::string& s) {
    size_t p = s.find("NAME:SID:22[");
    if (p == std::string::npos) return "";
    size_t e = s.find("]22", p);
    if (e == std::string::npos) return "";
    return s.substr(p + 12, e - (p + 12));
}

// 扫描出所有 VID:xxx 和 SID:22[...]22，按出现顺序 push 到 args（变量已解引用）
void collect_args(const std::string& s, MemorySpace& mem, std::vector<std::string>& args) {
    size_t p = 0;
    while (p < s.size()) {
        size_t vs = s.find("VID:", p);
        size_t ss = s.find("SID:22[", p);
        if (vs == std::string::npos && ss == std::string::npos) break;

        if (ss != std::string::npos && (vs == std::string::npos || ss < vs)) {
            size_t e = s.find("]22", ss + 7);
            if (e == std::string::npos) break;
            args.push_back(s.substr(ss + 7, e - ss - 7));
            p = e + 3;
        } else {
            size_t e;
            std::string name = peek_var_after(s, vs, &e);
            if (!name.empty() && (isdigit((unsigned char)name[0]) || name[0] == '-'))
                args.push_back(name);
            else
                args.push_back(resolve_deep_value(name, mem));
            p = e;
        }
    }
}