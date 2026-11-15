// ============================================================
// 内建原语统一入口
// ============================================================
bool try_eval_builtin(const std::string& s, MemorySpace& mem, std::string& out) {
    if (try_eval_builtin_str (s, mem, out)) return true;
    if (try_eval_builtin_num (s, mem, out)) return true;
    if (try_eval_builtin_misc(s, mem, out)) return true;
    return false;
}