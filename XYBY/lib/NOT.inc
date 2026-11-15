// ============================================================
// NOT.inc —— [NOT] 逻辑非（前缀）
// ============================================================
bool handleNotInBool(const std::string& expr, MemorySpace& mem,
                     bool use_deep, bool& out) {
    if (expr.rfind("[NOT]", 0) != 0) return false;

    out = !eval_bool_expr(expr.substr(5), mem, use_deep);
    return true;
}