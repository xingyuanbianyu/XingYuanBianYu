// ============================================================
// 最基础的字面量 / 变量解析（兜底）
// ============================================================
std::string eval_literal(const std::string& s, MemorySpace& mem) {
    // ============================================================
    // COOID_N：注释锚点
    // ============================================================
    if (s.rfind("COOID_", 0) == 0) {
        Variable* var = find_variable(s, mem);
        if (var && !var->value.empty()) return var->value;

        try {
            int idx = std::stoi(s.substr(6));
            if (idx < 0) idx += (int)g_cooid_table.size();
            if (idx >= 0 && idx < (int)g_cooid_table.size()) {
                return g_cooid_table[idx];
            }
        } catch (...) {}
        return "";
    }

    // ---------- 原有字面量分支 ----------
    if (s.rfind("SID:22[", 0) == 0) {
        size_t end = s.find("]22");
        if (end == std::string::npos) return s.substr(7);
        std::string content = s.substr(7, end - 7);
        for (char& ch : content) if (ch == '\x01') ch = '\n';
        return content;
    }

    // NID 整数化
    if (s.rfind("NID:", 0) == 0) {
        std::string num = s.substr(4);
        size_t b = num.find_first_not_of(" \t");
        size_t e = num.find_last_not_of(" \t");
        if (b == std::string::npos) return "0";
        num = num.substr(b, e - b + 1);
        try {
            long long v = std::stoll(num);
            return std::to_string(v);
        } catch (...) {
            return num;
        }
    }

    if (s.rfind("FLT:", 0) == 0) return s.substr(4);

    // ============================================================
    // VID:xxx —— 变量读取 / 属性读取
    // ============================================================
    if (s.rfind("VID:", 0) == 0) {
        std::string name = trim(s.substr(4));

        // 【新增】属性访问：obj.field
        if (name.find('.') != std::string::npos) {
            size_t dot = name.find('.');
            std::string obj_name   = trim(name.substr(0, dot));
            std::string field_name = trim(name.substr(dot + 1));

            Variable* obj = find_variable(obj_name, mem);
            if (obj && obj->type == "INSTANCE") {
                auto it = g_instance_fields.find(obj->address);
                if (it != g_instance_fields.end()) {
                    auto fit = it->second.find(field_name);
                    if (fit != it->second.end()) return fit->second.value;
                }
            }
            return "None";
        }

        // 嵌套 COOID_N
        if (name.rfind("COOID_", 0) == 0) {
            return eval_literal(name, mem);
        }

        // ① 当前作用域变量表
        auto it = mem.primary_memory.find(name);
        if (it != mem.primary_memory.end()) {
            return it->second.value;
        }

        // ② global_root 兜底
        if (mem.global_root) {
            auto git = mem.global_root->primary_memory.find(name);
            if (git != mem.global_root->primary_memory.end()) {
                return git->second.value;
            }
        }

        // ③ 深查
        return resolve_deep_value(name, mem);
    }
    return s;
}