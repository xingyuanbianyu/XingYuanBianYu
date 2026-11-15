bool handleWhile(const std::string& line, const std::vector<std::string>& lines, size_t line_idx, int brace_level, MemorySpace& mem, int& skip_level) {
    if (line.find("KID:WHILE") == std::string::npos) return false;
    if (line.find("KID:ENDWHILE") != std::string::npos) return false;

    size_t start = line.find("KID:WHILE");
    size_t cond_pos = line.find("COND:", start);
    if (cond_pos == std::string::npos) { skip_level = brace_level + 1; return true; }

    size_t cond_start = cond_pos + 5;
    size_t cond_end = line.find("}", cond_start);
    if (cond_end == std::string::npos) { skip_level = brace_level + 1; return true; }
    std::string cond = line.substr(cond_start, cond_end - cond_start);

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
    size_t endwhile_idx = lines.size();
    for (size_t k = body_start; k < lines.size(); ++k) {
        std::string t = trim(lines[k]);
        if (t == "{") depth++;
        else if (t == "}") {
            if (depth == 0) {
                for (size_t m = k + 1; m < lines.size(); ++m) {
                    std::string t2 = trim(lines[m]);
                    if (t2.empty()) continue;
                    if (t2.find("KID:ENDWHILE") != std::string::npos) endwhile_idx = m;
                    break;
                }
                break;
            }
            depth--;
        }
    }

    std::string cond_line = "ID:{KID:IF COND:" + cond + "}MEID:LID:0";
    if (!evaluateWhileCondition(cond_line, mem)) { skip_level = brace_level + 1; return true; }

    LoopFrame frame;
    frame.type = LoopFrame::Type::WHILE;
    frame.while_cond = cond;
    frame.body_brace_idx = body_brace_idx;
    frame.body_start = body_start;
    frame.endfor_idx = endwhile_idx; 
    frame.copy_depth = mem.copy_history.size();
    frame.brace_level = brace_level; // 🚀 记录初始层级
    frame.iterations = 0;
    m_loop_stack.push_back(frame);
    return true;
}

bool handleEndWhile(const std::string& line, MemorySpace& mem, size_t& i) {
    if (line.find("KID:ENDWHILE") == std::string::npos) return false;
    if (m_loop_stack.empty()) return true;

    LoopFrame& frame = m_loop_stack.back();
    if (frame.type != LoopFrame::Type::WHILE) return true;

    frame.iterations++;
    if (frame.iterations > MAX_LOOP_ITERATIONS) {
        while (mem.copy_history.size() > frame.copy_depth) mem.copy_history.pop_back();
        i = frame.endfor_idx; m_loop_stack.pop_back(); return true;
    }

    // 🚀 先判断条件，再清副本
    std::string cond_line = "ID:{KID:IF COND:" + frame.while_cond + "}MEID:LID:0";
    bool passed = evaluateWhileCondition(cond_line, mem);
    while (mem.copy_history.size() > frame.copy_depth) mem.copy_history.pop_back();

    if (!passed) { m_loop_stack.pop_back(); return true; }
    i = frame.body_start - 1;
    return true;
}