// =====================================================================
// lib/DEL.inc - 删除变量 / 删除已注册函数
// 语法：
//   del x           → 删除变量 x
//   del "func"      → 删除名为 func 的函数（如果存在）
//   del(x)          → 同上
// =====================================================================

// 尝试从一行 AST 里取出要删除的名字
// 依次尝试：VID:xxx  → SID:22[xxx]22
std::string _del_extractName(const std::string& line) {
    // 1. VID:xxx
    size_t vpos = line.find("VID:");
    if (vpos != std::string::npos) {
        size_t s = vpos + 4, e = s;
        while (e < line.size() && (isalnum((unsigned char)line[e]) || line[e] == '_')) e++;
        if (e > s) return line.substr(s, e - s);
    }
    // 2. SID:22[xxx]22
    size_t spos = line.find("SID:22[");
    if (spos != std::string::npos) {
        size_t s = spos + 7;
        size_t e = line.find("]22", s);
        if (e != std::string::npos) return line.substr(s, e - s);
    }
    return "";
}

// ---------- DEL 处理器 ----------
bool handleDel(const std::string& line, MemorySpace& mem) {
    if (line.find("KID:DEL") == std::string::npos) return false;

    std::string name = _del_extractName(line);
    if (name.empty()) {
        std::cerr << "DEL 错误: 缺少变量名或函数名" << std::endl;
        return true;
    }

    bool removed = false;

    // ---------- 1. 删变量：主内存 ----------
    if (mem.primary_memory.erase(name) > 0) removed = true;

    // ---------- 2. 删变量：所有副本层 ----------
    for (auto& layer : mem.copy_history) {
        if (layer.erase(name) > 0) removed = true;
    }

    // ---------- 3. 删已注册函数 ----------
    // 你的 DEF.inc 里如果用的是 m_functions 这张表，这里就能直接删。
    // 如果你的函数表叫别的名字（比如 m_func_table / module_memory 等），
    // 把这行改成对应名字即可。
    //
    // if (m_functions.erase(name) > 0) removed = true;

    // ---------- 4. 清理地址映射 ----------
    // 变量删了，地址映射也一并清掉，避免下次同名变量复用旧地址
    m_variable_address_map.erase(name);

    // ---------- 5. 诊断 ----------
    if (!removed) {
        // 这里只提示，不算错误，因为 del 函数时变量本就不存在
        // 若你想严格报错，把下面这行改成 std::cerr
        // std::cerr << "DEL 警告: '" << name << "' 不存在" << std::endl;
    }

    return true;
}