// ============================================================
// 递归求值布尔表达式
// ============================================================
bool eval_bool_expr(const std::string& expr, MemorySpace& mem, bool use_deep) {
    std::string e = trim(expr);
    if (e.empty()) return false;

    if (e.find("BOOL:TRUE")  != std::string::npos) return true;
    if (e.find("BOOL:FALSE") != std::string::npos) return false;
    if (e.find("BOOL:NONE")  != std::string::npos) return false;

    bool out = false;

    // ① 逻辑运算（优先级最低，先拆）
    if (handleOrInBool (e, mem, use_deep, out)) return out;
    if (handleAndInBool(e, mem, use_deep, out)) return out;
    if (handleNotInBool(e, mem, use_deep, out)) return out;

    // ============================================================
    // 🚀 新增：单变量 / 单字面量求值
    //   如果既没有比较运算符，也没有逻辑运算符，它就是一个纯粹的变量或值
    //   直接取它的值，判断真值即可（如 a or b 里的 a、b）
    // ============================================================
    bool has_cmp = (e.find("==") != std::string::npos || e.find("<") != std::string::npos ||
                    e.find(">") != std::string::npos || e.find("[EQ]") != std::string::npos ||
                    e.find("[LT]") != std::string::npos || e.find("[GT]") != std::string::npos ||
                    e.find("[GE]") != std::string::npos || e.find("[LE]") != std::string::npos ||
                    e.find("[NE]") != std::string::npos);
    bool has_logic = (e.find("[OR]") != std::string::npos || e.find("[AND]") != std::string::npos ||
                      e.find("[NOT]") != std::string::npos);

    if (!has_cmp && !has_logic) {
        // 提取变量名（处理 "VID:" 前缀）
        std::string vname = e;
        if (vname.rfind("VID:", 0) == 0) vname = vname.substr(4);

        std::string val;
        if (use_deep) {
            val = resolve_deep_value(vname, mem);
        } else {
            auto it = mem.primary_memory.find(vname);
            if (it != mem.primary_memory.end()) {
                val = (it->second.type == "REF") ? resolve_deep_value(it->second.value, mem)
                                                 : it->second.value;
            } else {
                val = "None";
            }
        }

        // 判断真值（兼容 "True"/"False"/数字/空串）
        if (val == "True"  || val == "true")  return true;
        if (val == "False" || val == "false" || val == "None" || val == "none" || val.empty())
            return false;
        try { return std::stod(val) != 0; } catch (...) { return true; }
    }

    // ② 比较运算（原有逻辑）
    std::string var_name = extractVarName(e);
    auto expected = extractExpectedValue(e);
    std::string op = extractCmpOp(e);

    std::string actual;
    if (use_deep) {
        actual = resolve_deep_value(var_name, mem);
    } else {
        auto it = mem.primary_memory.find(var_name);
        if (it == mem.primary_memory.end()) {
            actual = "None";
        } else if (it->second.type == "REF") {
            actual = resolve_deep_value(it->second.value, mem);
        } else {
            actual = it->second.value;
        }
    }
    return compare_values(actual, expected.second, op);
}