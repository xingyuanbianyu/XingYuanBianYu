bool handlePass(const std::string& line, MemorySpace& mem) {
    if (line.find("KID:PASS") == std::string::npos) return false;
    if (!m_last_print_output.empty()) {
        std::cout << m_last_print_output << std::endl;
        m_last_print_output.clear();
    }
    return true;
}