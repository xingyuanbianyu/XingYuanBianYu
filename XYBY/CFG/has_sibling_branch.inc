// ============================================================
// 判断 if 块后面有没有 elif / else 兄弟分支
// ============================================================
bool hasSiblingBranch(const std::vector<std::string>& lines, size_t if_index, int brace_level) {
    int depth = brace_level;
    size_t j = if_index + 1;

    // 先跳过当前 if 的整个块
    for (; j < lines.size(); ++j) {
        std::string tl = trim(lines[j]);
        if (tl == "{") {
            depth++;
        } else if (tl == "}") {
            depth--;
            if (depth == brace_level) break;
        }
    }

    // 看紧跟的非空行是不是 ELIF / ELSE
    for (size_t k = j + 1; k < lines.size(); ++k) {
        std::string tl = trim(lines[k]);
        if (tl.empty()) continue;
        if (tl.find("KID:ELIF") != std::string::npos ||
            tl.find("KID:ELSE") != std::string::npos) {
            return true;
        }
        break;
    }
    return false;
}