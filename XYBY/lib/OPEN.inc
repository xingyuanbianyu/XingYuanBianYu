// ============================================================
// OPEN.inc —— FID:OPEN 文件操作
//   模式：r / rb / w / wb / a / ab / s(mkdir) / o(touch) / lines
//   两种操作入口：
//     open(...) as x      → 保存操作（AS 字段，handleOpen 自己处理）
//     x = open(...)       → 赋值操作（[3D]，handleOpen 让路给 handleAssign）
//
//   🚀 文件访问策略：先 VFS，后物理文件系统
// ============================================================

// ---------- 统一参数提取（VID / SID / [xxx] / 裸词） ----------
static std::string open_extract_param(const std::string& line,
                                      const std::string& key,
                                      MemorySpace& mem) {
    size_t pos = line.find(key);
    if (pos == std::string::npos) return "";
    size_t nc = pos + key.size();
    while (nc < line.size() && (line[nc] == ' ' || line[nc] == '\t')) nc++;

    // VID:变量
    if (line.compare(nc, 4, "VID:") == 0) {
        size_t s = nc + 4, e = s;
        while (e < line.size() &&
               (isalnum((unsigned char)line[e]) || line[e] == '_' || line[e] == '.')) e++;
        std::string var = line.substr(s, e - s);
        std::string v = resolve_deep_value(var, mem);
        return (v == "None" || v.empty()) ? var : v;
    }
    // SID:22[...]22
    if (line.compare(nc, 4, "SID:") == 0) {
        size_t lb = line.find('[', nc);
        size_t rb = line.find("]22", lb);
        if (lb != std::string::npos && rb != std::string::npos)
            return line.substr(lb + 1, rb - lb - 1);
    }
    // [xxx]
    if (nc < line.size() && line[nc] == '[') {
        size_t rb = line.find(']', nc);
        if (rb != std::string::npos) return line.substr(nc + 1, rb - nc - 1);
    }
    // 裸词 / 路径
    size_t e = nc;
    while (e < line.size() && !isspace((unsigned char)line[e]) &&
           line[e] != '}' && line[e] != ',') e++;
    return line.substr(nc, e - nc);
}

// ---------- 路径预处理：只做 ~ 展开 ----------
static std::string open_resolve_path(const std::string& target) {
    if (target.empty()) return target;
    std::string t = target;

    if (t[0] == '~') {
        const char* h = getenv("HOME");
        if (h) t = std::string(h) + t.substr(1);
    }
    return t;
}

// ---------- 物理回退路径：相对路径基于 g_script_dir ----------
static std::string open_physical_path(const std::string& target) {
    if (target.empty()) return target;
    std::string t = target;

    if (t[0] == '~') {
        const char* h = getenv("HOME");
        if (h) t = std::string(h) + t.substr(1);
    }

    if (t[0] != '/') {
        const std::string& base = g_script_dir.empty() ? g_user_cwd : g_script_dir;
        t = base + "/" + t;
    }

    std::error_code ec;
    auto p = fs::weakly_canonical(t, ec);
    return ec ? t : p.string();
}

// ---------- 统一读取：先 VFS，后物理 ----------
static bool open_read_all(const std::string& vfs_path,
                          const std::string& phys_path,
                          std::string& out) {
    // 🚀 第一步：尝试 VFS
    if (VFS::getInstance().get_node(vfs_path)) {
        out = VFS::getInstance().read_file(vfs_path);
        return true;
    }

    // 🚀 第二步：回退物理文件系统
    FILE* fp = fopen(phys_path.c_str(), "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    out.clear();
    if (sz > 0) {
        out.resize(sz);
        fread(&out[0], 1, sz, fp);
    }
    fclose(fp);
    return true;
}

// ---------- 统一写入：先 VFS，后物理 ----------
//   返回 true = 成功（无论走 VFS 还是物理）
static bool open_write_all(const std::string& vfs_path,
                           const std::string& phys_path,
                           const std::string& content,
                           const std::string& mode) {
    // 🚀 先判断 VFS 父目录是否存在
    std::string vfs_parent = "/";
    {
        size_t pos = vfs_path.find_last_of('/');
        if (pos != std::string::npos && pos > 0) vfs_parent = vfs_path.substr(0, pos);
    }
    auto vfs_parent_node = VFS::getInstance().get_node(vfs_parent);

    if (vfs_parent_node) {
        // ============================================
        // 走 VFS
        // ============================================
        if (mode == "s") {
            // mkdir
            if (!VFS::getInstance().get_node(vfs_path)) {
                VFS::getInstance().create_directory(vfs_path);
            }
            return true;
        }
        if (mode == "o") {
            // touch
            if (!VFS::getInstance().get_node(vfs_path)) {
                VFS::getInstance().create_file(vfs_path, "");
            }
            return true;
        }

        // 写文件前确保文件存在
        if (!VFS::getInstance().get_node(vfs_path)) {
            VFS::getInstance().create_file(vfs_path, "");
        }

        if (mode == "w" || mode == "wb") {
            VFS::getInstance().write_file(vfs_path, content);
        } else if (mode == "a" || mode == "ab") {
            VFS::getInstance().append_file(vfs_path, content);
        }
        return true;
    }

    // ============================================
    // 🚀 回退物理文件系统
    // ============================================
    if (mode == "s") {
        std::error_code ec;
        fs::create_directories(phys_path, ec);
        return true;
    }
    if (mode == "o") {
        if (!fs::exists(phys_path)) {
            std::ofstream ofs(phys_path);
            ofs.close();
        }
        return true;
    }

    // 写文件前确保父目录存在
    {
        fs::path p(phys_path);
        if (p.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
        }
    }

    std::ios::openmode om = std::ios::out;
    if      (mode == "w")  om = std::ios::out | std::ios::trunc;
    else if (mode == "a")  om = std::ios::out | std::ios::app;
    else if (mode == "ab") om = std::ios::out | std::ios::app | std::ios::binary;
    else if (mode == "wb") om = std::ios::out | std::ios::binary | std::ios::trunc;

    std::ofstream ofs(phys_path, om);
    if (ofs) {
        ofs << content;
        ofs.flush();
    }
    ofs.close();
    return true;
}

// ---------- 主入口 ----------
bool handleOpen(const std::string& line, MemorySpace& mem) {
    if (line.find("FID:OPEN") == std::string::npos) return false;

    // ============================================================
    // 赋值语句 x = open(...) 交给 handleAssign 处理
    // ============================================================
    if (line.find("[3D]") != std::string::npos) return false;

    std::string mode     = open_extract_param(line, "MODE:",   mem);
    std::string raw_target = open_extract_param(line, "TARGET:", mem);
    std::string as_var   = extractAsVar(line);
    std::string write    = open_extract_param(line, "WRITE:",  mem);

    if (mode.empty())     mode = "r";
    if (raw_target.empty()) return true;

    // ============================================================
    // 🚀 生成两个路径：
    //    vfs_path  → VFS 优先
    //    phys_path → 物理回退
    // ============================================================
    std::string expanded  = open_resolve_path(raw_target);
    std::string vfs_path  = VFS::getInstance().normalize_path(expanded);
    std::string phys_path = open_physical_path(expanded);

    std::string out_val;
    bool is_read_op = false;

    // =========================================================
    // 读模式
    // =========================================================
    if (mode == "r" || mode == "rb" || mode == "lines") {
        if (!open_read_all(vfs_path, phys_path, out_val)) {
            out_val = "Error: Cannot read file " + raw_target;
        }
        is_read_op = true;
    }
    // =========================================================
    // 写模式
    // =========================================================
    else {
        open_write_all(vfs_path, phys_path, write, mode);
        is_read_op = false;
    }

    // =========================================================
    // 结果写入变量 / 直接打印
    // =========================================================
    if (!as_var.empty()) {
        Variable v;
        v.type    = "STR";
        v.value   = out_val;
        v.address = allocate_address(as_var);
        v.scope   = (g_func_depth > 0) ? VarScope::FUNCTION : VarScope::GLOBAL;
        mem.primary_memory[as_var] = v;
    } else if (is_read_op) {
        std::cout << out_val << std::endl;
    }

    return true;
}