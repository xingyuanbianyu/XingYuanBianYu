// =====================================================================
// IMPORT.inc —— 模块加载与重构版
//   🚀 文件访问策略：先 VFS，后物理文件系统
// =====================================================================
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>

// ============================================================
// 🚀 读取模块文件的全部行：先 VFS，后物理文件系统
//    本函数不依赖 Executor 成员 → 保持自由函数
// ============================================================
static bool import_read_lines(const std::string& module_path,
                              std::vector<std::string>& out_lines) {
    out_lines.clear();
    if (module_path.empty()) return false;

    // ============================================================
    // 🚀 第一步：尝试 VFS
    // ============================================================
    {
        std::string vfs_path = VFS::getInstance().normalize_path(module_path);
        if (VFS::getInstance().get_node(vfs_path)) {
            std::string content = VFS::getInstance().read_file(vfs_path);
            std::stringstream ss(content);
            std::string line;
            while (std::getline(ss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                out_lines.push_back(line);
            }
            return true;
        }
    }

    // ============================================================
    // 🚀 第二步：回退物理文件系统
    // ============================================================
    std::string phys = module_path;

    if (!phys.empty() && phys[0] == '~') {
        const char* h = getenv("HOME");
        if (h) phys = std::string(h) + phys.substr(1);
    }

    if (!phys.empty() && phys[0] != '/') {
        const std::string& base = g_script_dir.empty() ? g_user_cwd : g_script_dir;
        phys = base + "/" + phys;
    }

    std::error_code ec;
    auto p = std::filesystem::weakly_canonical(phys, ec);
    if (!ec) phys = p.string();

    std::ifstream ifs(phys);
    if (!ifs.is_open()) return false;

    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out_lines.push_back(line);
    }
    return true;
}

// ============================================================
// 主入口（不加 Executor::，让它自动成为类成员函数）
// ============================================================
bool handleImport(const std::string& line, MemorySpace& mem) {
    if (line.find("ID:{IMP:") == std::string::npos) return false;

    size_t start = line.find("IMP:") + 4;
    size_t end = line.find('}', start);
    if (end == std::string::npos) return true;
    std::string module_name = trim(line.substr(start, end - start));

    // ---------- 后缀判定 ----------
    bool is_xyd = false;
    bool is_xyb = false;
    if (module_name.size() >= 4) {
        std::string ext = module_name.substr(module_name.size() - 4);
        if (ext == ".xyd") is_xyd = true;
        else if (ext == ".xyb") is_xyb = true;
    }

    ConfigType mod_type = ConfigType::COLLECTIVE_LOCAL;
    if (is_xyd) mod_type = ConfigType::MONOLITHIC_LOCAL;
    m_module_types[module_name] = mod_type;

    // =======================================================
    // 读取模块文件（先 VFS 后物理）
    // =======================================================
    std::vector<std::string> raw_lines;
    if (!import_read_lines(module_name, raw_lines)) {
        std::cerr << "[IMPORT] 无法打开模块文件: " << module_name << std::endl;
        return true;
    }

    if (raw_lines.empty()) return true;

    // =======================================================
    // 分流处理：.xyd 校验，.xyb 直通
    // =======================================================
    if (is_xyd) {
        ModuleHeader header = parse_module_header(raw_lines);
        if (!header.valid) {
            std::cerr << "[IMPORT] 模块 " << module_name << " 头部定义无效，拒绝加载。" << std::endl;
            return true;
        }
        std::cerr << "[IMPORT] 开始加载模块: " << module_name 
                  << " | 类型: " << header.type 
                  << " | 前后缀规则: " << header.prefix_rule 
                  << " | 出品方: " << header.origin << std::endl;
    } else if (is_xyb) {
        // .xyb 直接调用，跳过头部校验
    }

    // 3. 防循环导入
    if (m_executing_modules.count(module_name)) return true;
    m_executing_modules.insert(module_name);

    // =======================================================
    // 从 modules_ast 中获取解析好的 AST
    // =======================================================
    if (!modules_ast || modules_ast->find(module_name) == modules_ast->end()) {
        std::cerr << "[IMPORT] 模块 " << module_name << " 未预解析。" << std::endl;
        m_executing_modules.erase(module_name);
        return true;
    }

    const std::vector<std::string>& ast_lines = (*modules_ast)[module_name];

    size_t skip_head = 0;
    while (skip_head < ast_lines.size() &&
           ast_lines[skip_head].find("KID:") == std::string::npos) {
        skip_head++;
    }

    // 遍历 AST，注册函数
    for (size_t i = skip_head; i < ast_lines.size(); ++i) { 
        size_t skip_to = i;
        handleDef(ast_lines[i], ast_lines, i, 0, skip_to);
        i = skip_to;
    }

    // 如果是 .xyb 文件，立刻执行模块体
    if (is_xyb) {
        std::vector<std::string> body_ast(ast_lines.begin() + skip_head, ast_lines.end());
        executeBlock(body_ast, mem);
    }

    m_executing_modules.erase(module_name);

    // 登记变量到全局索引
    for (auto& pair : mem.primary_memory) global_variable_index[pair.first] = &pair.second;
    if (!mem.copy_history.empty()) {
        for (auto& pair : mem.copy_history.back()) global_variable_index[pair.first] = &pair.second;
    }

    return true;
}