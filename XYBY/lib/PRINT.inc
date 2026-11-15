// ============================================================
// PRINT.inc —— FID:PRINT 输出处理
// ============================================================
bool handlePrint(const std::string& line, MemorySpace& mem) {
    if (line.find("FID:PRINT") == std::string::npos) return false;

    std::string as_var = extractAsVar(line);

    std::string content = line.substr(line.find("FID:PRINT") + 9);
    size_t end_content = content.find("}}MEID:");
    if (end_content == std::string::npos) end_content = content.find("}");
    if (end_content != std::string::npos) content = content.substr(0, end_content);
    content = trim(content);

    // ============================================================
    // 🚀 precision 检测（加入安全转换）
    // ============================================================
    int precision = -1;
    size_t last_space = content.find_last_of(' ');
    if (last_space != std::string::npos) {
        std::string last_token = content.substr(last_space + 1);
        if (last_token.rfind("NID:", 0) == 0) {
            std::string prefix = content.substr(0, last_space);
            bool has_builtin = (prefix.find("FID:")    != std::string::npos ||
                                prefix.find("SID:22[") != std::string::npos ||
                                prefix.find("TMPL:")   != std::string::npos);
            if (!has_builtin) {
                try {
                    int p = std::stoi(last_token.substr(4));
                    if (p >= 0 && p <= 15) {
                        precision = p;
                        content = trim(prefix);
                    }
                } catch (...) {
                    // 忽略无效的精度参数，避免崩溃
                }
            }
        }
    }

    auto restore_newlines = [](std::string s) -> std::string {
        for (char& ch : s) if (ch == '\x01') ch = '\n';
        return s;
    };

    auto render_elem = [&](const std::string& raw) -> std::string {
        std::string v = eval_expr(raw, mem);
        if (raw.rfind("NID:", 0) == 0) {
            size_t fnz = v.find_first_not_of('0');
            v = (fnz == std::string::npos) ? "0" : v.substr(fnz);
        }
        return restore_newlines(v);
    };

    std::string output_val = "";

    bool has_op = false;
    for (const char* op : {"[ADD]", "[SUB]", "[MUL]", "[DIV]", "[IDIV]", "[POW]",
                           "[GT]",  "[LT]",  "[GE]",  "[LE]",  "[EQ]",  "[NE]",
                           "[OR]", "[AND]", "[NOT]"}) {
        if (content.find(op) != std::string::npos) { has_op = true; break; }
    }

    bool has_sid = (content.find("SID:22[") != std::string::npos);

    if (content.rfind("TMPL:", 0) == 0 ||
        content.rfind("FID:", 0) == 0 ||
        content.find("FID:INPUT") != std::string::npos ||
        has_sid ||
        has_op) {
        output_val = restore_newlines(eval_expr(content, mem));
    } else {
        std::stringstream ss(content);
        std::string part;
        while (ss >> part) {
            if (part.rfind("SID:22[", 0) == 0) {
                size_t end = part.find("]22");
                if (end == std::string::npos) end = part.size();
                std::string seg = part.substr(7, end - 7);
                output_val += restore_newlines(seg);
            }
            else if (part.rfind("NID:", 0) == 0) {
                std::string val = part.substr(4);
                size_t first_non_zero = val.find_first_not_of('0');
                output_val += (first_non_zero != std::string::npos) ? val.substr(first_non_zero) : "0";
            }
            else if (part.rfind("FLT:", 0) == 0) {
                output_val += part.substr(4);
            }
            else if (part.rfind("VID:", 0) == 0) {
                std::string vn = part.substr(4);

                // 属性访问：obj.field
                if (vn.find('.') != std::string::npos) {
                    size_t dot = vn.find('.');
                    std::string obj_name   = trim(vn.substr(0, dot));
                    std::string field_name = trim(vn.substr(dot + 1));

                    Variable* obj = find_variable(obj_name, mem);
                    if (obj && obj->type == "INSTANCE") {
                        auto it = g_instance_fields.find(obj->address);
                        if (it != g_instance_fields.end()) {
                            auto fit = it->second.find(field_name);
                            if (fit != it->second.end()) {
                                output_val += restore_newlines(fit->second.value);
                                continue;
                            }
                        }
                    }
                    output_val += "None";
                    continue;
                }

                // 原有变量逻辑
                Variable* var = find_variable(vn, mem);

                if (!var && mem.global_root) {
                    auto git = mem.global_root->primary_memory.find(vn);
                    if (git != mem.global_root->primary_memory.end()) {
                        var = &git->second;
                    }
                }

                if (var && var->type == "LIST") {
                    std::vector<std::string> elems;
                    std::string cur;
                    for (size_t i = 4; i < var->value.size(); ++i) {
                        if (var->value[i] == '\x01') { elems.push_back(cur); cur.clear(); }
                        else cur += var->value[i];
                    }
                    if (!cur.empty() || !elems.empty()) elems.push_back(cur);

                    output_val += "[";
                    for (size_t i = 0; i < elems.size(); ++i) {
                        if (i > 0) output_val += ", ";
                        output_val += render_elem(elems[i]);
                    }
                    output_val += "]";
                } else if (var) {
                    output_val += restore_newlines(var->value);
                } else {
                    output_val += restore_newlines(resolve_deep_value(vn, mem));
                }
            }
            else if (part.rfind("SLICE:", 0) == 0) {
                size_t c1 = part.find(':', 6);
                if (c1 != std::string::npos) {
                    std::string vn    = part.substr(6, c1 - 6);
                    std::string idx_s = part.substr(c1 + 1);

                    Variable* var = find_variable(vn, mem);
                    if (!var && mem.global_root) {
                        auto git = mem.global_root->primary_memory.find(vn);
                        if (git != mem.global_root->primary_memory.end()) {
                            var = &git->second;
                        }
                    }

                    // 🚀 安全处理索引越界和非法格式
                    int idx = -1;
                    try {
                        idx = std::stoi(idx_s);
                    } catch (...) {
                        continue; // 非法索引直接跳过
                    }

                    if (var && var->type == "LIST") {
                        std::vector<std::string> elems;
                        std::string cur;
                        for (size_t i = 4; i < var->value.size(); ++i) {
                            if (var->value[i] == '\x01') { elems.push_back(cur); cur.clear(); }
                            else cur += var->value[i];
                        }
                        if (!cur.empty() || !elems.empty()) elems.push_back(cur);

                        if (idx < 0) idx += (int)elems.size();
                        if (idx >= 0 && idx < (int)elems.size()) {
                            output_val += render_elem(elems[idx]);
                        }
                    } else {
                        std::string val = var ? var->value : resolve_deep_value(vn, mem);
                        if (idx < 0) idx += (int)val.size();
                        if (idx >= 0 && idx < (int)val.size()) {
                            output_val += val.substr(idx, 1);
                        }
                    }
                }
            }
        }
    }

    // ============================================================
    // 🚀 格式化与打印（加入安全转换）
    // ============================================================
    if (is_numeric(output_val)) {
        try {
            double d = std::stod(output_val);
            if (precision != -1) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(precision) << d;
                output_val = oss.str();
            } else if (output_val.find('.') != std::string::npos) {
                output_val = format_double(d);
            }
        } catch (...) {
            // 如果 stod 失败，保留原字符串
        }
    }

    if (!as_var.empty()) {
        registerVariable(mem, as_var, {"STR", output_val, 0});
    } else {
        if (!output_val.empty() && output_val.back() == '\n') {
            std::cout << output_val;
        } else {
            std::cout << output_val << std::endl;
        }
        m_last_print_output = output_val;
    }
    return true;
}