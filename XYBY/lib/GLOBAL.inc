// ============================================================
// GLOBAL.inc —— GLOBAL 关键字处理
//   遇到 GLOBAL:TRUE 的赋值行，直接求值并写入全局层。
//   函数内调用也走这条路径：eval_user_function 共享 mem，
//   写 mem.primary_memory 就等于写全局。
// ============================================================
bool handleGlobal(const std::string& line, MemorySpace& mem) {
    if (line.find("GLOBAL:TRUE") == std::string::npos) return false;

    // 只处理赋值行；GLOBAL 标记出现在其他语句上（如 def）交给别人
    if (line.find("[3D]") == std::string::npos) return false;

    std::string var_name = extractVarName(line);
    if (var_name.empty()) return true;

    // 提取 [3D] 右侧表达式
    size_t eq_pos = line.find("[3D]");
    std::string right = line.substr(eq_pos + 4);

    size_t end = right.find("}MEID:");
    if (end == std::string::npos) end = right.find("}");
    if (end != std::string::npos) right = right.substr(0, end);
    right = trim(right);

    // ---------- 求值（复用常见路径）----------
    std::string val;
    if (right.find("FID:CALL") != std::string::npos) {
        val = eval_user_function(right, mem);
    } else if (right.rfind("TMPL:", 0) == 0) {
        val = eval_template(right, mem);
    } else {
        val = eval_expr(right, mem);
    }

    // ---------- 组装 Variable ----------
    Variable v;
    v.type     = is_numeric(val) ? "INT" : "STR";
    v.value    = val;
    v.address  = allocate_address(var_name);
    v.is_const = (line.find("CONST:TRUE") != std::string::npos);

    // ---------- 直接写全局层 ----------
    mem.primary_memory[var_name] = v;

    return true;
}