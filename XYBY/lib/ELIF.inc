// ============================================================
// ELIF.inc —— KID:ELIF
// ============================================================
bool handleElif(const std::string& line, MemorySpace& mem,
                std::map<int, bool>& if_passed,
                int brace_level, int& skip_level) {
    if (line.find("KID:ELIF") == std::string::npos) return false;

    // 【修复】不再 push 空作用域
    if (if_passed[brace_level]) {
        skip_level = brace_level + 1;
    } else {
        bool passed = evaluateCondition(line, mem);
        if_passed[brace_level] = passed;
        if (!passed) skip_level = brace_level + 1;
    }
    return true;
}