// ==========================================
// 基础工具函数（全局函数，不要加 const）
// 注意：trim 已存在于 lib/utils.inc 中，此处不重复定义
// ==========================================

int get_decimal_places(const std::string& s) {
    size_t dot_pos = s.find('.');
    if (dot_pos == std::string::npos) return 0;
    return s.length() - dot_pos - 1;
}

std::string format_double(double val) {
    std::string s = std::to_string(val);
    if (s.find('.') != std::string::npos) {
        s.erase(s.find_last_not_of('0') + 1, std::string::npos);
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

bool is_numeric(const std::string& s) {
    if (s.empty()) return false;
    char* end = nullptr;
    std::strtod(s.c_str(), &end);
    return end != s.c_str() && *end == '\0';
}

std::string extractVarName(const std::string& line) {
    size_t pos = line.find("VID:");
    if (pos == std::string::npos) return "";
    size_t start = pos + 4;
    size_t end = start;
    // 【改动】放行 '.'，让 self.name / obj.field 完整识别
    while (end < line.size() &&
           (isalnum((unsigned char)line[end]) || line[end] == '_' || line[end] == '.')) end++;
    return trim(line.substr(start, end - start));
}

std::pair<std::string, std::string> extractExpectedValue(const std::string& line) {
    size_t nid_pos = line.find("NID:");
    size_t sid_pos = line.find("SID:22[");
    size_t flt_pos = line.find("FLT:");

    if (sid_pos != std::string::npos) {
        size_t start = sid_pos + 7;
        size_t end = line.find("]22", start);
        if (end != std::string::npos) return {"STR", line.substr(start, end - start)};
    } else if (flt_pos != std::string::npos) {
        size_t start = flt_pos + 4;
        size_t end = start;
        while (end < line.size() && (isdigit((unsigned char)line[end]) || line[end] == '.')) end++;
        return {"FLOAT", line.substr(start, end - start)};
    } else if (nid_pos != std::string::npos) {
        size_t start = nid_pos + 4;
        size_t end = start;
        while (end < line.size() && isdigit((unsigned char)line[end])) end++;
        std::string val = line.substr(start, end - start);
        size_t first_non_zero = val.find_first_not_of('0');
        val = (first_non_zero != std::string::npos) ? val.substr(first_non_zero) : "0";
        return {"INT", val};
    }
    return {"UNKNOWN", ""};
}