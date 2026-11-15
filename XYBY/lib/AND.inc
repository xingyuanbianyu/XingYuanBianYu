// ============================================================
// AND.inc —— [AND] 逻辑与
// ============================================================
bool handleAndInBool(const std::string& expr, MemorySpace& mem,
                     bool use_deep, bool& out) {
    size_t pos = expr.rfind("[AND]");
    if (pos == std::string::npos) return false;

    out = eval_bool_expr(expr.substr(0, pos), mem, use_deep) &&
          eval_bool_expr(expr.substr(pos + 5), mem, use_deep);
    return true;
}