// ============================================================
// 表达式求值：主派发
//   ⚠️ 逻辑运算符求值函数（try_eval_or / try_eval_and / try_eval_not）
//      已经定义在 eval_cmp.inc 里，这里不再重复定义！
// ============================================================
std::string eval_expr(const std::string& expr, MemorySpace& mem) {
    std::string s = trim(expr);
    if (s.empty()) return "0";

    // ============================================================
    // 🚀 【终极修复】拦截并执行 {KID:TRF ...} 节点
    // ============================================================
    if (s.rfind("{KID:TRF", 0) == 0) {
        size_t ops_start = s.find("OPS:[");
        size_t ops_end = s.find("] VARS:[");
        size_t vars_end = s.find("]}", ops_end);

        if (ops_start != std::string::npos && ops_end != std::string::npos && vars_end != std::string::npos) {
            std::string ops_str = s.substr(ops_start + 5, ops_end - ops_start - 5);
            std::string vars_str = s.substr(ops_end + 8, vars_end - ops_end - 8);
            
            std::vector<std::string> vars;
            std::stringstream ss_vars(vars_str);
            std::string item;
            while (std::getline(ss_vars, item, ',')) vars.push_back(trim(item));
            
            std::vector<std::string> ops;
            std::stringstream ss_ops(ops_str);
            while (std::getline(ss_ops, item, ',')) ops.push_back(trim(item));

            if (vars.size() >= 2 && ops.size() >= 1) {
                // 🚀 剥去外层大括号的小工具
                auto strip_braces = [](std::string str) {
                    str = trim(str);
                    if (str.size() >= 2 && str.front() == '{' && str.back() == '}') {
                        return str.substr(1, str.size() - 2);
                    }
                    return str;
                };

                for (size_t i = 0; i < ops.size(); ++i) {
                    // 🚀 使用 strip_braces 处理后再求值
                    std::string left_val = eval_expr(strip_braces(vars[i]), mem);
                    std::string right_val = eval_expr(strip_braces(vars[i+1]), mem);
                    std::string op_val = eval_expr(strip_braces(ops[i]), mem); 

                    // 洗数据：去掉可能存在的双引号
                    auto clean_str = [](std::string str) {
                        str = trim(str);
                        if (str.size() >= 2 && str.front() == '"' && str.back() == '"') {
                            str = str.substr(1, str.size() - 2);
                        }
                        return str;
                    };
                    left_val = clean_str(left_val);
                    right_val = clean_str(right_val);
                    op_val = clean_str(op_val);

                    std::cout << "[DEBUG-TRF] 左边:[" << left_val << "] 操作符:[" << op_val << "] 右边:[" << right_val << "]" << std::endl;

                    bool step_passed = false;
                    
                    if (op_val == "==" || op_val == "eq") {
                        try {
                            step_passed = (std::stod(left_val) == std::stod(right_val));
                        } catch (...) {
                            step_passed = (left_val == right_val);
                        }
                    } else if (op_val == "!=" || op_val == "ne") {
                        try {
                            step_passed = (std::stod(left_val) != std::stod(right_val));
                        } catch (...) {
                            step_passed = (left_val != right_val);
                        }
                    } else if (op_val == ">") {
                        step_passed = (std::stod(left_val) > std::stod(right_val));
                    } else if (op_val == "<") {
                        step_passed = (std::stod(left_val) < std::stod(right_val));
                    } else if (op_val == ">=") {
                        step_passed = (std::stod(left_val) >= std::stod(right_val));
                    } else if (op_val == "<=") {
                        step_passed = (std::stod(left_val) <= std::stod(right_val));
                    } else if (op_val == "tr" || op_val == "and") {
                        step_passed = (left_val == right_val);
                    }
                    
                    if (!step_passed) {
                        return "False"; 
                    }
                }
                return "True"; 
            }
        }
        return "False";
    }
    // ============================================================

    // 【新增 ⓪】方法调用 —— 必须最先判断
    if (s.find("FID:METHOD") != std::string::npos) {
        return eval_method_call(s, mem);
    }

    // ① INPUT
    if (isInputExpression(s)) return evalInputExpression(s);

    // ② 内建原语
    std::string builtin;
    if (try_eval_builtin(s, mem, builtin)) return builtin;

    // ③ TMPL:
    if (s.rfind("TMPL:", 0) == 0) return eval_template(s, mem);

    // ④ 🚀 逻辑运算符（优先级最低，先拆）
    //    顺序严格：OR → AND → NOT
    if (auto r = try_eval_or(s, mem);  r) return *r;
    if (auto r = try_eval_and(s, mem); r) return *r;
    if (auto r = try_eval_not(s, mem); r) return *r;

    // ⑤ 比较运算符
    if (auto r = try_eval_comparison(s, mem); r) return *r;

    // ============================================================
    // 🚀 ⑤.5 单一 FID:CALL 优先处理
    //    如果整串就是一个函数调用（只含一个 NAME:SID:22[...]），
    //    直接交给 FID:CALL 分支，避免 try_eval_arithmetic 把
    //    参数内部的 [SUB] 误认为顶层运算符，例如：
    //    fact(n - 1) → FID:CALL ... ARGS:VID:n[SUB]NID:0001
    // ============================================================
    if (s.rfind("FID:CALL", 0) == 0) {
        size_t first  = s.find("NAME:SID:22[");
        size_t second = (first != std::string::npos)
                      ? s.find("NAME:SID:22[", first + 1)
                      : std::string::npos;
        if (first != std::string::npos && second == std::string::npos) {
            std::string fname = peek_call_name(s);
            std::string out;
            if (try_eval_native_io(s, fname, mem, out)) return out;
            if (m_functions.find(fname) != m_functions.end())
                return eval_user_function(s, mem);
        }
    }

    // ⑥ 算术运算符 —— 放在 FID:CALL 之前
    if (auto r = try_eval_arithmetic(s, mem); r) return *r;

    // ⑦ FID:CALL（多个 CALL 混合场景，或原生 I/O）
    if (s.find("NAME:SID:22[") != std::string::npos) {
        std::string fname = peek_call_name(s);
        std::string out;
        if (try_eval_native_io(s, fname, mem, out)) return out;
        if (m_functions.find(fname) != m_functions.end())
            return eval_user_function(s, mem);
    }

    // ============================================================
    // 🚀 ⑧ 纯变量引用 —— 放在字面量处理之前拦截
    // ============================================================
    if (s.rfind("VID:", 0) == 0) {
        std::string var_name = s.substr(4);
        size_t valid_len = 0;
        while (valid_len < var_name.size() &&
               (isalnum((unsigned char)var_name[valid_len]) ||
                var_name[valid_len] == '_' || var_name[valid_len] == '.')) {
            valid_len++;
        }
        var_name = var_name.substr(0, valid_len);

        Variable* v = find_variable(var_name, mem);
        if (v) return resolve_deep_value(var_name, mem);
        return var_name;
    }

    if (!s.empty() &&
        (isalpha((unsigned char)s[0]) || s[0] == '_') &&
        s.find_first_of("[:(){}+\\-*/<>=!&|,]") == std::string::npos &&
        s != "True" && s != "False" && s != "None" && s != "true" && s != "false") {
        Variable* v = find_variable(s, mem);
        if (v) return resolve_deep_value(s, mem);
    }

    // ⑨ 字面量
    return eval_literal(s, mem);
}