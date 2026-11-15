// ============================================================
// ASYNC.inc —— async 关键字处理器
// 当遇到 KID:DEF + ASYNC:TRUE 时，抢先拦截，完成异步函数注册。
// 注册后返回 true，让 executeBlock 跳过 handleDef 的重复处理。
// ============================================================
bool handleAsync(const std::string& line,
                 const std::vector<std::string>& lines,
                 size_t line_idx, int brace_level, size_t& skip_to,
                 MemorySpace& mem) {
    // 只处理带 ASYNC:TRUE 标记的 KID:DEF 节点
    if (line.find("KID:DEF") == std::string::npos) return false;
    if (line.find("ASYNC:TRUE") == std::string::npos) return false;

    // 调用 handleDef 完成注册（handleDef 会解析 ASYNC:TRUE 并记录到 FunctionDef 里）
    return handleDef(line, lines, line_idx, brace_level, skip_to);
}