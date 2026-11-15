bool handleBreak(const std::string& line, MemorySpace& mem, size_t& i, int brace_level, int& skip_level) {
    if (line.find("KID:BREAK") == std::string::npos) return false;
    if (m_loop_stack.empty()) return true;

    LoopFrame& frame = m_loop_stack.back();
    while (mem.copy_history.size() > frame.copy_depth) mem.copy_history.pop_back();

    // 🚀 弹出循环栈，直接跳到 ENDFOR 后
    i = frame.endfor_idx; 
    m_loop_stack.pop_back();
    return true;
}