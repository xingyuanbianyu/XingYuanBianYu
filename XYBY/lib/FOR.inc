bool handleFor(const std::string& line, const std::vector<std::string>& lines, size_t line_idx, int brace_level, MemorySpace& mem, int& skip_level) {
    if (line.find("KID:FOR") == std::string::npos) return false;
    if (line.find("KID:ENDFOR") != std::string::npos) return false;

    std::string var_name = handleForInClause(line);
    if (var_name.empty()) { skip_level = brace_level + 1; return true; }

    size_t body_brace_idx = line_idx + 1;
    for (; body_brace_idx < lines.size(); ++body_brace_idx) {
        std::string t = trim(lines[body_brace_idx]);
        if (t == "{") break;
        if (t.empty()) continue;
        break;
    }
    if (body_brace_idx >= lines.size() || trim(lines[body_brace_idx]) != "{") { skip_level = brace_level + 1; return true; }
    size_t body_start = body_brace_idx + 1;

    int depth = 0;
    size_t endfor_idx = lines.size();
    for (size_t k = body_start; k < lines.size(); ++k) {
        std::string t = trim(lines[k]);
        if (t == "{") depth++;
        else if (t == "}") {
            if (depth == 0) {
                for (size_t m = k + 1; m < lines.size(); ++m) {
                    std::string t2 = trim(lines[m]);
                    if (t2.empty()) continue;
                    if (t2.find("KID:ENDFOR") != std::string::npos) endfor_idx = m;
                    break;
                }
                break;
            }
            depth--;
        }
    }

    LoopFrame frame;
    frame.type = LoopFrame::Type::FOR;
    frame.var = var_name;
    frame.body_brace_idx = body_brace_idx;
    frame.body_start = body_start;
    frame.endfor_idx = endfor_idx;
    frame.copy_depth = mem.copy_history.size();
    frame.brace_level = brace_level; // 🚀 记录初始层级
    frame.iterations = 0;

    if (line.find("FID:ITER") != std::string::npos) {
        std::string src_node;
        size_t sp = line.find("SRC:");
        if (sp != std::string::npos) {
            size_t ss = sp + 4;
            size_t se = line.find("}", ss);
            if (se == std::string::npos) se = line.size();
            src_node = line.substr(ss, se - ss);
        }
        if (src_node.empty()) { skip_level = brace_level + 1; return true; }

        std::string src_val = eval_expr(src_node, mem);
        std::vector<std::string> elems;

        if (src_val.rfind("LST:", 0) == 0) {
            std::string cur;
            for (size_t k = 4; k < src_val.size(); ++k) {
                if (src_val[k] == '\x01') { elems.push_back(cur); cur.clear(); }
                else cur += src_val[k];
            }
            if (!cur.empty() || elems.empty()) elems.push_back(cur);
        } else {
            size_t k = 0;
            while (k < src_val.size()) {
                unsigned char ch = src_val[k];
                int len = 1;
                if      ((ch & 0x80) == 0)    len = 1;
                else if ((ch & 0xE0) == 0xC0) len = 2;
                else if ((ch & 0xF0) == 0xE0) len = 3;
                else if ((ch & 0xF8) == 0xF0) len = 4;
                if (k + len > src_val.size()) break;
                elems.push_back("SID:22[" + src_val.substr(k, len) + "]22");
                k += len;
            }
        }

        if (elems.empty()) { skip_level = brace_level + 1; return true; }

        // 🚀 修复类型污染
        std::string first_val = eval_expr(elems[0], mem);
        std::string type_str = (!first_val.empty() && first_val.find_first_not_of("0123456789.-") == std::string::npos) ? "INT" : "STR";
        mem.primary_memory[var_name] = {type_str, first_val, 0};

        frame.is_iter = true;
        frame.iter_values = elems;
        frame.iter_index = 0;
        m_loop_stack.push_back(frame);
        return true;
    }

    int64_t start_val = 0, end_val = 0;
    handleForRangeClause(line, start_val, end_val);
    mem.primary_memory[var_name] = {"INT", std::to_string(start_val), 0};

    if (start_val >= end_val) { skip_level = brace_level + 1; return true; }

    frame.is_iter = false;
    frame.current = start_val;
    frame.end = end_val;
    m_loop_stack.push_back(frame);
    return true;
}

bool handleEndFor(const std::string& line, MemorySpace& mem, size_t& i) {
    if (line.find("KID:ENDFOR") == std::string::npos) return false;
    if (m_loop_stack.empty()) return true;

    LoopFrame& frame = m_loop_stack.back();
    if (frame.type != LoopFrame::Type::FOR) return true;

    frame.iterations++;
    if (frame.iterations > MAX_LOOP_ITERATIONS) {
        while (mem.copy_history.size() > frame.copy_depth) mem.copy_history.pop_back();
        i = frame.endfor_idx; m_loop_stack.pop_back(); return true;
    }

    while (mem.copy_history.size() > frame.copy_depth) mem.copy_history.pop_back();

    if (frame.is_iter) {
        frame.iter_index++;
        if (frame.iter_index >= frame.iter_values.size()) { m_loop_stack.pop_back(); return true; }
        std::string new_val = eval_expr(frame.iter_values[frame.iter_index], mem);
        std::string type_str = (!new_val.empty() && new_val.find_first_not_of("0123456789.-") == std::string::npos) ? "INT" : "STR";
        mem.primary_memory[frame.var] = {type_str, new_val, 0};
        i = (frame.body_brace_idx > 0) ? (frame.body_brace_idx - 1) : 0;
        return true;
    }

    frame.current++;
    if (frame.current >= frame.end) { m_loop_stack.pop_back(); return true; }
    mem.primary_memory[frame.var] = {"INT", std::to_string(frame.current), 0};
    i = (frame.body_brace_idx > 0) ? (frame.body_brace_idx - 1) : 0;
    return true;
}