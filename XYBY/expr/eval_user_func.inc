// ============================================================
// 用户自定义函数求值器（完美隔离栈版，修复链接错误）
// ============================================================
std::string eval_user_function(const std::string& expr, MemorySpace& mem) {
    std::string fname = peek_call_name(expr);
    if (fname.empty()) return "None";

    auto it_func = m_functions.find(fname);
    if (it_func == m_functions.end()) return "Error: Function not found";
    const FunctionDef& fn = it_func->second;

    // 实参求值（在外层 mem 里做）
    std::vector<std::string> actual_values;
    size_t apos = expr.find("ARGS:");
    if (apos != std::string::npos) {
        size_t a_start = apos + 5;
        size_t a_end = expr.find(" AS:", a_start);
        if (a_end == std::string::npos) a_end = expr.find("}", a_start);
        if (a_end == std::string::npos) a_end = expr.length();
        std::string args_str = trim(expr.substr(a_start, a_end - a_start));
        std::stringstream ss(args_str);
        std::string arg_part;
        while (std::getline(ss, arg_part, ',')) {
            arg_part = trim(arg_part);
            if (!arg_part.empty())
                actual_values.push_back(eval_expr(arg_part, mem));
        }
    }

    // 🚀 记录当前作用域深度，并压入函数专属作用域！
    size_t saved_depth = mem.copy_history.size();
    mem.copy_history.push_back({}); 
    g_func_depth++;

    // 形参直接放进刚刚压入的局部栈
    for (size_t i = 0; i < fn.params.size(); ++i) {
        std::string v = (i < actual_values.size()) ? actual_values[i] : "None";
        Variable pv;
        pv.type    = "STR";
        pv.value   = v;
        pv.address = allocate_address(fn.params[i]);
        pv.scope   = VarScope::FUNCTION;
        mem.copy_history.back()[fn.params[i]] = pv;
    }

    std::string result = "None";
    mem.has_returned = false;
    mem.last_return_value = "";

    try {
        executeBlock(fn.body, mem);
    } catch (const ReturnSignal& rs) {
        result = rs.value;
    }

    // 🚀 函数退出，精准回滚栈深度！局部变量瞬间销毁，绝不影响全局！
    while (mem.copy_history.size() > saved_depth) {
        mem.copy_history.pop_back();
    }

    g_func_depth--;
    return result;
}

// ============================================================
// run_method_core —— 执行 owner_cls.mname(args)，含继承链递归
// ============================================================
std::string run_method_core(const std::string& owner_cls,
                            const std::string& mname,
                            const std::vector<std::string>& actual_values,
                            uint32_t self_address,
                            MemorySpace& mem) {
    if (!m_classes.count(owner_cls) || !m_classes[owner_cls].methods.count(mname))
        return "None";

    const FunctionDef& fn = m_classes[owner_cls].methods[mname];

    // ---- 找父方法归属类 ----
    std::string parent_cls;
    if (!fn.inherit_from.empty()) {
        parent_cls = fn.inherit_from;
    } else {
        std::set<std::string> visited;
        std::vector<std::string> queue;
        for (auto& p : m_classes[owner_cls].parents) queue.push_back(p);

        while (!queue.empty()) {
            std::string cur = queue.front();
            queue.erase(queue.begin());
            if (visited.count(cur) || !m_classes.count(cur)) continue;
            visited.insert(cur);

            if (m_classes[cur].methods.count(mname)) {
                parent_cls = cur;
                break;
            }
            for (auto& p : m_classes[cur].parents) queue.push_back(p);
        }
    }

    // ---- 父方法递归跑 ----
    std::vector<std::string> final_args = actual_values;
    if (!parent_cls.empty() && m_classes.count(parent_cls) && m_classes[parent_cls].methods.count(mname)) {
        std::string parent_ret = run_method_core(parent_cls, mname, actual_values, self_address, mem);
        final_args.clear();
        for (size_t k = 0; k + 1 < fn.params.size(); ++k)
            final_args.push_back(parent_ret);
    }

    // 🚀 记录栈深，压入专属作用域
    size_t saved_depth = mem.copy_history.size();
    mem.copy_history.push_back({});
    g_func_depth++;

    Variable sv;
    sv.type    = "INSTANCE";
    sv.value   = owner_cls;
    sv.address = self_address;
    sv.scope   = VarScope::FUNCTION;
    mem.copy_history.back()["self"] = sv;

    for (size_t i = 1; i < fn.params.size(); ++i) {
        std::string v = (i - 1 < final_args.size()) ? final_args[i - 1] : "None";
        Variable pv;
        pv.type    = "STR";
        pv.value   = v;
        pv.address = allocate_address(fn.params[i]);
        pv.scope   = VarScope::FUNCTION;
        mem.copy_history.back()[fn.params[i]] = pv;
    }

    std::string result = "None";
    mem.has_returned = false;
    mem.last_return_value = "";

    try { executeBlock(fn.body, mem); }
    catch (const ReturnSignal& rs) { result = rs.value; }

    // 🚀 弹栈销毁局部环境
    while (mem.copy_history.size() > saved_depth) {
        mem.copy_history.pop_back();
    }

    g_func_depth--;
    return result;
}

// ============================================================
// 🚀 补全被误删的 eval_method_call 函数体
// eval_method_call —— 执行 obj.method(args) 并返回结果
// ============================================================
std::string eval_method_call(const std::string& expr, MemorySpace& mem) {
    // ---- 提取 obj ----
    std::string obj_name;
    size_t op = expr.find("OBJ:VID:");
    if (op != std::string::npos) {
        size_t s = op + 8, e = s;
        while (e < expr.size() &&
               (isalnum((unsigned char)expr[e]) || expr[e] == '_')) e++;
        obj_name = expr.substr(s, e - s);
    }
    if (obj_name.empty()) return "None";

    // ---- 提取方法名 ----
    std::string mname;
    size_t np = expr.find("NAME:SID:22[");
    if (np != std::string::npos) {
        size_t s = np + 12;
        size_t e = expr.find("]22", s);
        if (e != std::string::npos) mname = expr.substr(s, e - s);
    }
    if (mname.empty()) return "None";

    // ---- 查 obj ----
    Variable* obj = find_variable(obj_name, mem);
    if (!obj || obj->type != "INSTANCE") {
        fprintf(stderr, "[METHOD] obj '%s' is not an instance\n", obj_name.c_str());
        return "None";
    }
    std::string cls = obj->value;

    // ---- 实参求值 ----
    std::vector<std::string> actual_values;
    size_t apos = expr.find("ARGS:");
    if (apos != std::string::npos) {
        size_t a_start = apos + 5;
        size_t a_end = expr.find(" AS:", a_start);
        if (a_end == std::string::npos) a_end = expr.find("}", a_start);
        if (a_end == std::string::npos) a_end = expr.length();
        std::string args_str = trim(expr.substr(a_start, a_end - a_start));
        std::stringstream ss(args_str);
        std::string ap;
        while (std::getline(ss, ap, ',')) {
            ap = trim(ap);
            if (!ap.empty()) actual_values.push_back(eval_expr(ap, mem));
        }
    }

    // ============================================================
    // 沿继承链 BFS 找"定义了这个方法的类"
    // ============================================================
    std::string owner_cls;
    {
        std::set<std::string> visited;
        std::vector<std::string> queue;
        queue.push_back(cls);

        while (!queue.empty()) {
            std::string cur = queue.front();
            queue.erase(queue.begin());
            if (visited.count(cur) || !m_classes.count(cur)) continue;
            visited.insert(cur);

            if (m_classes[cur].methods.count(mname)) {
                owner_cls = cur;
                break;
            }
            for (auto& p : m_classes[cur].parents) queue.push_back(p);
        }
    }
    if (owner_cls.empty()) {
        fprintf(stderr, "[METHOD] %s.%s not found\n", cls.c_str(), mname.c_str());
        return "None";
    }

    // ============================================================
    // 交给 run_method_core 递归执行
    // ============================================================
    return run_method_core(owner_cls, mname, actual_values, obj->address, mem);
}