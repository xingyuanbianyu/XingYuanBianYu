// ============================================================
// CLASS.inc —— 定义类 (KID:CLASS)
//   语法：class X:                      ← 无父类
//         class X(A, B, C):             ← 多继承
//   AST：ID:{KID:CLASS NAME:SID:22[X]22 PARENTS:SID:22[A, B, C]22}MEID:LID:N
// ============================================================
bool handleClass(const std::string& line, MemorySpace& mem, int brace_level) {
    if (line.find("KID:CLASS") == std::string::npos) return false;

    // ---------- 类名 ----------
    const std::string NAME_TAG = "NAME:SID:22[";   // 长度 12
    size_t name_pos = line.find(NAME_TAG);
    if (name_pos == std::string::npos) return true;
    size_t name_start = name_pos + NAME_TAG.length();
    size_t name_end = line.find("]22", name_start);
    if (name_end == std::string::npos) return true;
    std::string class_name = trim(line.substr(name_start, name_end - name_start));
    if (class_name.empty()) return true;

    m_classes[class_name].name = class_name;

    // ---------- 【新增】父类列表 ----------
    const std::string PARENT_TAG = "PARENTS:SID:22[";   // 长度 15
    size_t pp = line.find(PARENT_TAG);
    if (pp != std::string::npos) {
        size_t ps = pp + PARENT_TAG.length();
        size_t pe = line.find("]22", ps);
        if (pe != std::string::npos) {
            std::string list = line.substr(ps, pe - ps);
            // 逗号切分
            std::stringstream ss(list);
            std::string parent;
            while (std::getline(ss, parent, ',')) {
                parent = trim(parent);
                if (!parent.empty())
                    m_classes[class_name].parents.push_back(parent);
            }
        }
    }

    // ---------- 压栈 ----------
    g_class_stack.push_back(class_name);
    g_class_brace_stack.push_back(brace_level);

    // ---------- 内存注册 ----------
    Variable class_var;
    class_var.type    = "CLASS";
    class_var.value   = class_name;
    class_var.address = allocate_address(class_name);
    class_var.scope   = VarScope::GLOBAL;
    mem.primary_memory[class_name] = class_var;
    if (!mem.copy_history.empty())
        mem.copy_history.back()[class_name] = class_var;

    return true;
}