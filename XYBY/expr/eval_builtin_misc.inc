// ============================================================
// 杂项内建
// ⚠️ 依赖 eval_builtin_num.inc 里的 builtin_first_arg / builtin_arg_at
//    （include 顺序必须是 num 在 misc 之前）
// ============================================================
bool try_eval_builtin_misc(const std::string& s, MemorySpace& mem, std::string& out) {
    // --- BOOL ---
    if (s.rfind("FID:BOOL", 0) == 0) {
        std::string val = builtin_first_arg(s, mem);
        if (val.empty() || val == "0" || val == "None" || val == "False")
            out = "False";
        else
            out = "True";
        return true;
    }

    // --- ID ---
    // 语义：取变量的虚拟地址。字面量没有地址，返回 0。
    if (s.rfind("FID:ID", 0) == 0) {
        size_t p = s.find("VID:");
        if (p != std::string::npos) {
            std::string var_name = peek_var_after(s, p);
            out = std::to_string(allocate_address(var_name));
        } else {
            // 字面量：没有地址，返回 0
            out = "0";
        }
        return true;
    }

    // --- HASH ---
    if (s.rfind("FID:HASH", 0) == 0) {
        std::string val = builtin_first_arg(s, mem);
        std::hash<std::string> h;
        out = std::to_string((long long)h(val));
        return true;
    }

    // --- HELP ---
    // 语义：查变量详情，本质需要变量名。
    if (s.rfind("FID:HELP", 0) == 0) {
        size_t p = s.find("VID:");
        if (p != std::string::npos) {
            std::string var_name = peek_var_after(s, p);
            std::string val = resolve_deep_value(var_name, mem);
            out = "帮助: " + var_name + " = " + val;
        } else {
            // 字面量：直接展示
            std::string val = builtin_first_arg(s, mem);
            out = "帮助: 字面量 = " + val;
        }
        return true;
    }

    // --- WITH_READ ---
    if (s.rfind("FID:WITH_READ", 0) == 0) {
        out = with_read_all();
        return true;
    }

    // --- WITH_WRITE ---
    if (s.rfind("FID:WITH_WRITE", 0) == 0) {
        std::string content = builtin_first_arg(s, mem);
        write_data(content);
        out = "";
        return true;
    }

    return false;
}