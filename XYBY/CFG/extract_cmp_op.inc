// ============================================================
// 从 AST 行里提取比较运算符
// ============================================================
std::string extractCmpOp(const std::string& line) {
    if (line.find("[MATCH_R]") != std::string::npos) return "[EQ]";
    if (line.find("[MATCH_L]") != std::string::npos) return "[EQ]";
    if (line.find("[NE]")      != std::string::npos) return "[NE]";
    if (line.find("[GE]")      != std::string::npos) return "[GE]";
    if (line.find("[LE]")      != std::string::npos) return "[LE]";
    if (line.find("[GT]")      != std::string::npos) return "[GT]";
    if (line.find("[LT]")      != std::string::npos) return "[LT]";
    if (line.find("[EQ]")      != std::string::npos) return "[EQ]";
    if (line.find("[0x3D]")    != std::string::npos) return "[EQ]";
    return "[EQ]";
}