bool handleContinue(const std::string& line, MemorySpace& mem, size_t& i, int brace_level, int& skip_level) {
    if (line.find("KID:CONTINUE") == std::string::npos) return false;
    if (m_loop_stack.empty()) return true;

    LoopFrame& frame = m_loop_stack.back();
    while (mem.copy_history.size() > frame.copy_depth) mem.copy_history.pop_back();

    // 🚀 直接跳跃，不设置 skip_level
    i = frame.endfor_idx - 1; 
    return true;
}