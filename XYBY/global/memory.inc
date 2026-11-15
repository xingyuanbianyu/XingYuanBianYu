// ==========================================
// 内存地址分配
// ==========================================
uint32_t allocate_address(const std::string& raw_var_name) {
    std::string var_name = trim(raw_var_name);
    if (var_name.empty()) return 0;

    auto it_char = CHAR_ID_MAP.find(var_name);
    if (it_char != CHAR_ID_MAP.end()) {
        if (it_char->first == var_name) return it_char->second;
    }

    auto it = m_variable_address_map.find(var_name);
    if (it != m_variable_address_map.end()) return it->second;

    uint32_t assigned = m_next_address++;
    m_variable_address_map[var_name] = assigned;
    return assigned;
}

// ==========================================
// 深度解析变量值（处理 REF 引用）
// ==========================================
std::string resolve_deep_value(const std::string& var_name, MemorySpace& mem, int depth = 0) {
    if (depth > 100) return "[Error: Reference Loop]";
    if (var_name.empty() || var_name == "None") return var_name;

    Variable* found_var = nullptr;
    for (int i = (int)mem.copy_history.size() - 1; i >= 0; --i) {
        if (mem.copy_history[i].find(var_name) != mem.copy_history[i].end()) {
            found_var = &mem.copy_history[i][var_name];
            break;
        }
    }
    if (!found_var && mem.primary_memory.find(var_name) != mem.primary_memory.end()) {
        found_var = &mem.primary_memory[var_name];
    }
    if (!found_var) {
        // ============================================================
        // 【新增】COOID_N 兜底：未在内存中，去查全局注释表
        //   g_cooid_table 在 utils.inc 里定义
        // ============================================================
        if (var_name.rfind("COOID_", 0) == 0) {
            try {
                int idx = std::stoi(var_name.substr(6));
                if (idx < 0) idx += (int)g_cooid_table.size();
                if (idx >= 0 && idx < (int)g_cooid_table.size()) {
                    return g_cooid_table[idx];
                }
            } catch (...) {}
            return "None";
        }

        Variable none_var;
        none_var.type = "STR";
        none_var.value = "None";
        if (mem.copy_history.empty()) mem.primary_memory[var_name] = none_var;
        else                          mem.copy_history.back()[var_name] = none_var;
        return "None";
    }
    if (found_var->type == "REF") {
        std::string ref_name = found_var->value;
        if (ref_name == var_name) return "[Error: Self Reference]";
        return resolve_deep_value(ref_name, mem, depth + 1);
    }
    return found_var->value;
}

// ==========================================
// 查找变量（优先副本层，再主本）
// ==========================================
Variable* find_variable(const std::string& var_name, MemorySpace& mem) {
    for (int i = (int)mem.copy_history.size() - 1; i >= 0; --i) {
        auto it = mem.copy_history[i].find(var_name);
        if (it != mem.copy_history[i].end()) return &(it->second);
    }
    auto it = mem.primary_memory.find(var_name);
    if (it != mem.primary_memory.end()) return &(it->second);
    return nullptr;
}

// ==========================================
// 变量注册（写入主本 + 当前副本层）
// ==========================================
void registerVariable(MemorySpace& mem, const std::string& var_name, const Variable& var_val) {
    if (mem.primary_memory.find(var_name) == mem.primary_memory.end()) {
        mem.primary_memory[var_name] = var_val;
    }
    if (mem.copy_history.empty()) mem.copy_history.push_back({});
    mem.copy_history.back()[var_name] = var_val;
}