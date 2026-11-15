// ============================================================
// OR.inc —— [OR] 逻辑或
//   从表达式里找最后一个 [OR]，拆成左右两半递归求值
// ============================================================
bool handleOrInBool(const std::string& expr, MemorySpace& mem,
                    bool use_deep, bool& out) {
    size_t pos = expr.rfind("[OR]");
    if (pos == std::string::npos) return false;

    out = eval_bool_expr(expr.substr(0, pos), mem, use_deep) ||
          eval_bool_expr(expr.substr(pos + 4), mem, use_deep);
    return true;
}