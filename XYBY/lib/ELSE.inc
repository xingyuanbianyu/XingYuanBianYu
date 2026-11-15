// ============================================================
// ELSE.inc —— KID:ELSE
// ============================================================
bool handleElse(const std::string& line, MemorySpace& mem,
                std::map<int, bool>& if_passed,
                int brace_level, int& skip_level) {
    if (line.find("KID:ELSE") == std::string::npos) return false;

    // 【修复】不再 push 空作用域
    if (if_passed[brace_level]) skip_level = brace_level + 1;
    else                        if_passed[brace_level] = true;
    return true;
}