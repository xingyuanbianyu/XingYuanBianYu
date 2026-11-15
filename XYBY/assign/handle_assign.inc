// ============================================================
// 赋值语句 [3D] 的处理（绝对作用域隔离版）
// ============================================================
bool handleAssign(const std::string& line, MemorySpace& mem) {
    if (line.find("[3D]") == std::string::npos) return false;

    std::string var_name = extractVarName(line);
    size_t eq_pos = line.find("[3D]");
    std::string right_part = line.substr(eq_pos + 4);

    size_t end_of_expr = right_part.find("}MEID:");
    if (end_of_expr == std::string::npos) end_of_expr = right_part.find("}");
    if (end_of_expr != std::string::npos) right_part = right_part.substr(0, end_of_expr);
    right_part = trim(right_part);

    if (var_name.empty()) return true;

    // 🚀 判断是否为“声明”操作（AST 中带 [3D] 和 CONST 标识通常代表声明）
    bool is_declaration = (line.find("[3D]") != std::string::npos && line.find("CONST:") != std::string::npos);

    // 🚀 如果是声明，不管外层有没有同名变量，直接允许（允许遮蔽 Shadowing）
    // 🚀 只有在非声明（普通赋值如 x = 20）时，才检查 const 属性
    if (!is_declaration) {
        Variable* existing = find_variable(var_name, mem);
        if (existing && existing->is_const) {
            std::cout << "错误: const 变量 '" << var_name << "' 不可修改" << std::endl;
            return true;
        }
    }

    uint32_t addr = allocate_address(var_name);
    Variable new_var;
    new_var.address = addr;
    new_var.is_const = (line.find("CONST:TRUE") != std::string::npos);

    // =========================================================
    // 右侧表达式求值
    // =========================================================

    // ---------- KID:AWAIT ----------
    if (right_part.find("KID:AWAIT") != std::string::npos) {
        g_await_target_var = var_name;
        handleAwait(right_part, mem);
        std::string ret = mem.last_return_value;
        new_var.type  = is_numeric(ret) ? "INT" : "STR";
        new_var.value = ret;

        VarScope vscope = (g_func_depth > 0) ? VarScope::FUNCTION : VarScope::GLOBAL;
        new_var.scope = vscope;
        // 🚀 修复：写入正确的层级
        if (vscope == VarScope::FUNCTION && !mem.copy_history.empty()) {
            mem.copy_history.back()[var_name] = new_var;
        } else {
            mem.primary_memory[var_name] = new_var;
        }
        return true;
    }

    // ---------- FID:OPEN ----------
    if (right_part.find("FID:OPEN") != std::string::npos) {
        size_t p = right_part.find("FID:OPEN");
        std::string open_expr = right_part.substr(p);
        size_t e = open_expr.find("}");
        if (e != std::string::npos) open_expr = open_expr.substr(0, e);

        std::string tmp_line = open_expr + " AS:{KID:AS VALUE:VID:" + var_name + "}";
        handleOpen(tmp_line, mem);

        auto it = mem.primary_memory.find(var_name);
        if (it != mem.primary_memory.end()) {
            new_var.type  = it->second.type;
            new_var.value = it->second.value;
        }

        VarScope vscope = (g_func_depth > 0) ? VarScope::FUNCTION : VarScope::GLOBAL;
        new_var.scope = vscope;
        if (vscope == VarScope::FUNCTION && !mem.copy_history.empty()) {
            mem.copy_history.back()[var_name] = new_var;
        } else {
            mem.primary_memory[var_name] = new_var;
        }
        return true;
    }

    // ---------- FID:CALL ----------
    if (right_part.find("FID:CALL") != std::string::npos) {
        std::string call_name;
        size_t np = right_part.find("NAME:SID:22[");
        if (np != std::string::npos) {
            size_t s = np + 12;
            size_t e = right_part.find("]22", s);
            if (e != std::string::npos) call_name = right_part.substr(s, e - s);
        }

        if (!call_name.empty() && m_classes.count(call_name)) {
            new_var.type  = "INSTANCE";
            new_var.value = call_name;
        } else {
            std::string result = eval_user_function(right_part, mem);
            new_var.type  = is_numeric(result) ? "INT" : "STR";
            new_var.value = result;

            mem.last_return_value = result;
            g_last_xy_return_value = result;
        }
    }
    else if (right_part.rfind("TMPL:", 0) == 0) {
        std::string result = eval_template(right_part, mem);
        new_var.type  = "STR";
        new_var.value = result;
    }
    else if (right_part.find("FID:STRCAT") != std::string::npos) {
        std::string result = eval_expr(right_part, mem);
        new_var.type  = "STR";
        new_var.value = result;
    }
    else if (right_part.find("[ADD]")  != std::string::npos || right_part.find("[SUB]")  != std::string::npos ||
             right_part.find("[MUL]")  != std::string::npos || right_part.find("[DIV]")  != std::string::npos ||
             right_part.find("[IDIV]") != std::string::npos || right_part.find("[POW]")  != std::string::npos) {
        std::string result = eval_expr(right_part, mem);
        if (is_numeric(result))                new_var.type = "INT";
        else if (result.rfind("LST:", 0) == 0) new_var.type = "LIST";
        else                                   new_var.type = "STR";
        new_var.value = result;
    }
    else if (right_part.find("FID:INPUT") != std::string::npos) {
        std::string result = eval_expr(right_part, mem);
        if (result != "Error") {
            if      (right_part.find("TYPE:NID") != std::string::npos) new_var.type = "INT";
            else if (right_part.find("TYPE:FLT") != std::string::npos) new_var.type = "FLOAT";
            else                                                        new_var.type = "STR";
            new_var.value = result;
        } else return true;
    }
    else if (right_part.rfind("FID:LEN", 0) == 0 || right_part.rfind("FID:ABS", 0) == 0 ||
             right_part.rfind("FID:INT", 0) == 0 || right_part.rfind("FID:ORD", 0) == 0 ||
             right_part.rfind("FID:ROUND", 0) == 0 || right_part.rfind("FID:FLOOR", 0) == 0 ||
             right_part.rfind("FID:CEIL", 0) == 0) {
        new_var.type = "INT"; new_var.value = eval_expr(right_part, mem);
    }
    else if (right_part.rfind("FID:STR", 0) == 0 || right_part.rfind("FID:HEX", 0) == 0 ||
             right_part.rfind("FID:BIN", 0) == 0 || right_part.rfind("FID:OCT", 0) == 0 ||
             right_part.rfind("FID:CHR", 0) == 0 ||
             right_part.rfind("FID:UPPER", 0) == 0 || right_part.rfind("FID:LOWER", 0) == 0 ||
             right_part.rfind("FID:REVERSE", 0) == 0 ||
             right_part.rfind("FID:ASCII", 0) == 0 || right_part.rfind("FID:HASH", 0) == 0 ||
             right_part.rfind("FID:ID", 0) == 0    || right_part.rfind("FID:REPR", 0) == 0 ||
             right_part.rfind("FID:HELP", 0) == 0  ||
             right_part.rfind("FID:STRIP", 0) == 0 || right_part.rfind("FID:FIND", 0) == 0 ||
             right_part.rfind("FID:REPLACE", 0) == 0) {
        new_var.type = "STR"; new_var.value = eval_expr(right_part, mem);
    }
    else if (right_part.rfind("FID:BOOL", 0) == 0) {
        new_var.type = "BOOL"; new_var.value = eval_expr(right_part, mem);
    }
    else if (right_part.rfind("FID:MAX", 0) == 0 || right_part.rfind("FID:MIN", 0) == 0 ||
             right_part.rfind("FID:POW", 0) == 0 || right_part.rfind("FID:SUM", 0) == 0) {
        std::string result = eval_expr(right_part, mem);
        new_var.type = is_numeric(result) ? "INT" : "STR";
        new_var.value = result;
    }
    // 🚀 LST: 列表字面量 —— 深度求值每个元素
    else if (right_part.rfind("LST:", 0) == 0) {
        std::string cur, evaluated = "LST:";
        bool first = true;
        for (size_t k = 4; k <= right_part.size(); ++k) {
            if (k == right_part.size() || right_part[k] == '\x01') {
                if (!cur.empty()) {
                    std::string v = eval_expr(cur, mem);
                    if (!first) evaluated += '\x01';
                    evaluated += v;
                    first = false;
                }
                cur.clear();
            } else {
                cur += right_part[k];
            }
        }
        new_var.type  = "LIST";
        new_var.value = evaluated;
    }
    // 🚀 VID: 引用（深度解引用）
    else if (right_part.find("VID:") != std::string::npos) {
        size_t v_start = right_part.find("VID:") + 4;
        size_t v_end = v_start;
        while (v_end < right_part.size() &&
               (isalnum((unsigned char)right_part[v_end]) || right_part[v_end] == '_' || right_part[v_end] == '.')) v_end++;
        std::string ref_name = right_part.substr(v_start, v_end - v_start);

        Variable* src = find_variable(ref_name, mem);
        if (src) {
            std::string final_value = resolve_deep_value(ref_name, mem);

            if (src->type == "INT" || src->type == "FLOAT" || src->type == "BOOL") {
                new_var.type  = src->type;
                new_var.value = final_value;
            } else if (is_numeric(final_value)) {
                new_var.type  = "INT";
                new_var.value = final_value;
            } else {
                new_var.type  = "STR";
                new_var.value = final_value;
            }
        } else {
            new_var.type  = "REF";
            new_var.value = ref_name;
        }
    }
    else {
        auto expected = extractExpectedValue(line);
        if (expected.first != "UNKNOWN") {
            new_var.type  = expected.first;
            new_var.value = expected.second;
        }
    }

    // ============================================================
    // 【属性赋值】obj.field = value
    // ============================================================
    if (var_name.find('.') != std::string::npos) {
        size_t dot = var_name.find('.');
        std::string obj_name   = trim(var_name.substr(0, dot));
        std::string field_name = trim(var_name.substr(dot + 1));

        Variable* obj = find_variable(obj_name, mem);
        if (obj && obj->type == "INSTANCE") {
            if (new_var.type == "REF") {
                new_var.value = resolve_deep_value(new_var.value, mem);
                new_var.type  = "STR";
            }
            g_instance_fields[obj->address][field_name] = new_var;
        } else {
            fprintf(stderr, "[ATTR] obj '%s' is not an instance\n", obj_name.c_str());
        }
        return true;
    }

    // ============================================================
    // 🚀 普通变量：按 scope 归属写入
    // ============================================================
    VarScope vscope = VarScope::GLOBAL;
    
    // 如果是强制全局声明
    if (line.find("GLOBAL:TRUE") != std::string::npos) {
        vscope = VarScope::GLOBAL;
    } 
    // 否则，只要在函数内部，统统是局部变量
    else if (g_func_depth > 0) {
        vscope = VarScope::FUNCTION;
    }
    
    new_var.scope = vscope;

    // 🚀 核心修复：根据作用域决定写到哪个层级，彻底阻断污染！
    if (vscope == VarScope::FUNCTION && !mem.copy_history.empty()) {
        // 如果是在函数内，写到当前最顶层的局部环境栈里（只影响当前函数）
        mem.copy_history.back()[var_name] = new_var;
    } else {
        // 如果是全局变量，才写入主内存
        mem.primary_memory[var_name] = new_var;
    }

    return true;
}