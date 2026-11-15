// ============================================================
// DEF.inc —— 函数定义 / 调用
// ============================================================

// ---------- 解析参数列表 ----------
std::vector<std::string> parseParamList(const std::string& s) {
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) {
        part = trim(part);
        if (part.rfind("VID:", 0) == 0) part = part.substr(4);
        std::string clean;
        for (char c : part) {
            if (isalnum((unsigned char)c) || c == '_') clean += c;
            else break;
        }
        if (!clean.empty()) result.push_back(clean);
    }
    return result;
}

// ---------- 收集函数体 ----------
bool collectFunctionBody(const std::vector<std::string>& lines,
                         size_t def_index, int brace_level,
                         std::vector<std::string>& body,
                         size_t& end_index) {
    int depth = brace_level;
    size_t i = def_index;
    bool opened = false;

    std::string tl = trim(lines[i]);
    if (!tl.empty() && tl.back() == '{') {
        opened = true;
        depth++;
        i = def_index + 1;
    } else {
        ++i;
        for (; i < lines.size(); ++i) {
            tl = trim(lines[i]);
            if (tl == "{") { depth++; opened = true; ++i; break; }
            if (tl.empty()) continue;
            break;
        }
    }
    if (!opened) { end_index = def_index; return false; }

    size_t body_start = i;
    for (; i < lines.size(); ++i) {
        tl = trim(lines[i]);
        if (tl == "{") depth++;
        else if (tl == "}") {
            depth--;
            if (depth == brace_level) {
                body.assign(lines.begin() + body_start, lines.begin() + i);
                end_index = i;
                return true;
            }
        }
    }
    end_index = lines.size();
    return false;
}

// ============================================================
// KID:DEF —— 归属到类或全局
// ============================================================
bool handleDef(const std::string& line,
               const std::vector<std::string>& lines,
               size_t line_idx, int brace_level, size_t& skip_to) {
    if (line.find("KID:DEF") == std::string::npos) return false;

    FunctionDef fn;

    if (line.find("ASYNC:TRUE") != std::string::npos) {
        fn.is_async = true;
    }

    size_t npos = line.find("NAME:SID:22[");
    if (npos != std::string::npos) {
        size_t s = npos + 12;
        size_t e = line.find("]22", s);
        if (e != std::string::npos) fn.name = line.substr(s, e - s);
    }

    size_t ppos = line.find("PARAMS:");
    if (ppos != std::string::npos) {
        std::string params_str = line.substr(ppos + 7);
        size_t end_pos = params_str.find("}");
        if (end_pos != std::string::npos) params_str = params_str.substr(0, end_pos);
        fn.params = parseParamList(params_str);
    }

    // 读显式继承标记 "INHERIT:SID:22["
    {
        const std::string TAG = "INHERIT:SID:22[";
        size_t ipp = line.find(TAG);
        if (ipp != std::string::npos) {
            size_t s = ipp + TAG.length();
            size_t e = line.find("]22", s);
            if (e != std::string::npos)
                fn.inherit_from = line.substr(s, e - s);
        }
    }

    collectFunctionBody(lines, line_idx, brace_level, fn.body, skip_to);

    if (!g_class_stack.empty()) {
        m_classes[g_class_stack.back()].methods[fn.name] = fn;
    } else {
        m_functions[fn.name] = fn;
    }
    return true;
}

// ============================================================
// FID:CALL / FID:METHOD
// ============================================================
bool handleCall(const std::string& line, MemorySpace& mem) {

    // 先让路
    if (line.find("FID:PRINT") != std::string::npos) return false;
    if (line.find("CHID:")     != std::string::npos) return false;
    if (line.find("[3D]")      != std::string::npos) return false;

    // 【1】FID:METHOD
    if (line.find("FID:METHOD") != std::string::npos) {
        eval_method_call(line, mem);
        return true;
    }

    // 【2】FID:CALL
    if (line.find("FID:CALL")  == std::string::npos) return false;

    std::string fn_name;
    size_t npos = line.find("NAME:SID:22[");
    if (npos != std::string::npos) {
        size_t s = npos + 12;
        size_t e = line.find("]22", s);
        if (e != std::string::npos) fn_name = line.substr(s, e - s);
    } else return false;

    if (m_classes.count(fn_name)) return true;

    auto it = m_functions.find(fn_name);
    if (it == m_functions.end()) return false;
    const FunctionDef& fn = it->second;

    std::vector<std::string> actual_values;
    size_t apos = line.find("ARGS:");
    if (apos != std::string::npos) {
        size_t a_start = apos + 5;
        size_t a_end = line.find(" AS:", a_start);
        if (a_end == std::string::npos) a_end = line.find("}", a_start);
        if (a_end == std::string::npos) a_end = line.length();
        std::string args_str = trim(line.substr(a_start, a_end - a_start));
        std::stringstream ss(args_str);
        std::string arg_part;
        while (std::getline(ss, arg_part, ',')) {
            arg_part = trim(arg_part);
            if (!arg_part.empty())
                actual_values.push_back(eval_expr(arg_part, mem));
        }
    }

    // 🚀 记录当前作用域深度，用于在函数结束后强制截断
    size_t saved_copy_depth = mem.copy_history.size();

    // 🚀 创建一个新的局部作用域用于存放形参
    mem.copy_history.push_back({});

    // 🚀 将形参塞入新压入的局部作用域
    for (size_t i = 0; i < fn.params.size(); ++i) {
        std::string v = (i < actual_values.size()) ? actual_values[i] : "None";
        Variable pv;
        
        // 动态推导类型：如果全是数字，标为 INT，否则 STR
        if (!v.empty() && v.find_first_not_of("0123456789.-") == std::string::npos) {
            pv.type = "INT";
        } else {
            pv.type = "STR";
        }
        
        pv.value   = v;
        pv.address = allocate_address(fn.params[i]);
        pv.scope   = VarScope::FUNCTION;
        
        mem.copy_history.back()[fn.params[i]] = pv;
    }

    g_func_depth++;

    try {
        executeBlock(fn.body, mem);
    } catch (const ReturnSignal& sig) {
        g_last_xy_return_value = sig.value;
    } catch (...) {
        // 🚀 捕获其他异常（比如 break 或 continue 逃逸），防止作用域泄漏
        while (mem.copy_history.size() > saved_copy_depth) {
            mem.copy_history.pop_back();
        }
        g_func_depth--;
        throw; // 重新抛出给外层处理
    }

    // 🚀 强制清理作用域（无论是正常返回还是异常返回）
    while (mem.copy_history.size() > saved_copy_depth) {
        mem.copy_history.pop_back();
    }

    g_func_depth--;
    return true;
}