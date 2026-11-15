#include "../include/dcl_open.h"
#include "../include/dcl_exec.h"
#include "dcl_nano.h"
#include "../include/dcl_del.h"
#include "../../VFS/include/vfs.h"
#include "../../DD/include/disk_fs.h"
#include "../../AMM/include/amm_memory.h"
#include "../../DD/include/mem_driver.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <set>
#include <functional>
#include <memory>

// =========================================================================
// 🚀 前置声明（改成接收 vector 数组，以便接收参数）
// =========================================================================
std::string exec_cmd(DCLState& state, const std::vector<std::string>& args,
                     std::function<void(const std::string&)> xyn_executor);

// =========================================================================
// 外部声明
// =========================================================================
struct KernelPCB {
    int v_pid;
    std::string name;
    std::string cmd_name;
    bool is_running;
    VirtualMemoryBlock* vmem;
    bool pending_kill;
    int signal_type;
    std::vector<std::string> args;
};
extern std::map<int, KernelPCB> process_table;
extern int next_v_pid;
extern std::string spawn_virtual_process(const std::vector<std::string>& args, const std::string& vfs_path, bool is_daemon = false, bool is_thread = false);

// 🚀 uid → 账户名（定义在 dcl_drt.cpp）
extern std::string drt_get_name_by_uid(int uid);

// =========================================================================
// 🚀 归属映射：owner_id → "角色-账户名"
// =========================================================================
static std::string owner_id_to_role_name(int id) {
    std::string role;
    if (id < 0 || id > 300)           role = "R0";
    else if (id == 0)                 role = "R3";
    else if (id >= 1   && id <= 100)  role = "R4";
    else if (id >= 101 && id <= 300)  role = "R5";
    else                              role = "R3";

    if (role == "R0") return "R0";

    std::string uname = drt_get_name_by_uid(id);
    if (uname.empty()) return role;
    return role + "-" + uname;
}

// =========================================================================
// 🚀 盘符根识别与显示
// =========================================================================
static bool is_letter_drive_root(const std::string& vfs_path) {
    if (vfs_path.size() == 4 &&
        vfs_path[0] == '/' &&
        std::isalpha((unsigned char)vfs_path[1]) &&
        vfs_path[2] == ':' &&
        vfs_path[3] == '/') {
        return true;
    }
    return false;
}

static std::string display_path(const std::string& vfs_path) {
    if (is_letter_drive_root(vfs_path)) {
        return vfs_path.substr(1);
    }
    if (vfs_path.size() > 4 &&
        vfs_path[0] == '/' &&
        std::isalpha((unsigned char)vfs_path[1]) &&
        vfs_path[2] == ':' &&
        vfs_path[3] == '/') {
        return vfs_path.substr(1);
    }
    return vfs_path;
}

// =========================================================================
// 🚀 核心更新：统一路径前缀解析
// =========================================================================
static std::string resolve_path_prefix(const std::string& raw) {
    if (raw.empty()) return raw;

    // 1. 单独一个 "/"，代表干目录，直接返回
    if (raw == "/") return "/";

    std::string cwd = VFS::getInstance().get_cwd();
    if (cwd.empty()) cwd = "/";

    // 2. 单独一个 "~"，代表当前工作目录
    if (raw == "~") return cwd;

    // 3. 单独一个 "."，代表当前工作目录
    if (raw == ".") return cwd;

    // 4. "~/" 开头，代表家目录 /home/用户名
    if (raw.size() >= 2 && raw[0] == '~' && raw[1] == '/') {
        int uid = VFS::getInstance().get_user_id();
        std::string uname = drt_get_name_by_uid(uid);
        std::string home_dir = uname.empty() ? "/" : ("/home/" + uname);
        
        if (home_dir.back() == '/') return home_dir + raw.substr(2);
        return home_dir + raw.substr(1);
    }

    // 5. "./" 开头，代表当前目录下面的路径
    if (raw.size() >= 2 && raw[0] == '.' && raw[1] == '/') {
        if (cwd.back() == '/') return cwd + raw.substr(2);
        return cwd + raw.substr(1);
    }

    // 6. 绝对的 "/" 开头，以及其他相对路径，交给底层的 normalize_path 去处理
    return raw;
}

// =========================================================================
// 🚀 open 唯一入口
// =========================================================================
std::string open_cmd(DCLState& state, const std::vector<std::string>& raw_args,
                     std::function<void(const std::string&)> xyn_executor) {
    
    if (raw_args.empty()) return "用法: open <路径> [模式]";

    // 1. 智能拆分
    static const std::set<std::string> known_modes = {
        "nano", "exec", "del", "ls", "cd", "read", "info", "save", "auto"
    };

    std::string raw_path = raw_args[0];
    std::string raw_mode = "auto";
    std::vector<std::string> extra_args; // 🚀 存放要传给脚本的参数

    if (raw_args.size() > 1 && known_modes.count(raw_args.back())) {
        raw_mode = raw_args.back();
        // 🚀 掐头去尾：路径是第 0 个，最后一个是模式，中间的都是参数
        for (size_t i = 1; i + 1 < raw_args.size(); ++i) {
            extra_args.push_back(raw_args[i]);
        }
    } else {
        // 如果最后不是已知模式，则把后面的全拼成文件名（兼容带空格的文件名）
        for (size_t i = 1; i < raw_args.size(); ++i) {
            raw_path += " " + raw_args[i];
        }
    }

    if (raw_path.empty()) return "用法: open <路径> [模式]";

    // 2. 路径语义解析层
    std::string path_to_normalize = resolve_path_prefix(raw_path);

    if (path_to_normalize.find('\n') != std::string::npos ||
        path_to_normalize.find('\r') != std::string::npos) {
        return "错误: 路径或文件名中检测到换行符！请检查是否误将脚本内容作为文件名输入。";
    }

    // 3. 专属逻辑分发
    if (raw_mode == "exec") {
        std::vector<std::string> exec_args;
        exec_args.push_back(path_to_normalize); 
        for (const auto& a : extra_args) {
            exec_args.push_back(a);
        }
        return exec_cmd(state, exec_args, xyn_executor);
    }

    std::string vfs_path = VFS::getInstance().normalize_path(path_to_normalize);

    // ============================================================
    // 🚀 nano
    // ============================================================
    if (raw_mode == "nano") {
        if (!VFS::getInstance().check_resource_access(vfs_path)) {
            return "权限不足: 无法编辑该文件";
        }

        size_t last_slash = vfs_path.find_last_of('/');
        std::string filename_only = (last_slash == std::string::npos) ? vfs_path : vfs_path.substr(last_slash + 1);

        if (filename_only.empty() || filename_only == "/") {
            return "错误: 无效的文件名，不能为空";
        }

        if (!VFS::getInstance().get_node(vfs_path)) {
            std::string create_res = VFS::getInstance().create_file(vfs_path, "");
            if (!create_res.empty()) {
                return "错误: 无法创建文件 (" + vfs_path + "): " + create_res;
            }
        }

        return nano_cmd(state, vfs_path);
    }

    // 🚀 del 模式
    if (raw_mode == "del") {
        std::vector<std::string> del_args = { vfs_path };
        return del_cmd(state, del_args);
    }

    // 4. 核心 VFS 文件操作
    auto vfs_node = VFS::getInstance().get_node(vfs_path);
    if (!vfs_node) {
        return "错误: 找不到 (" + vfs_path + ")";
    }

    bool is_dir = (std::dynamic_pointer_cast<Directory>(vfs_node) != nullptr);
    std::string mode = raw_mode;
    if (mode == "auto") {
        mode = is_dir ? "cd" : "read";
    }

    // ============================================================
    // 🚀 read 模式
    // ============================================================
    if (mode == "read") {
        if (is_dir) return "错误: 这是一个目录，无法作为文件读取";

        if (!VFS::getInstance().check_resource_access(vfs_path)) {
            return "权限不足: 无法读取该文件";
        }

        std::string content = VFS::getInstance().read_file(vfs_path);
        state.data = content;
        state.path = vfs_path;
        state.cmd = "open";
        return content;
    }

    // ============================================================
    // 🚀 ls 模式 (终极斩杀空行版)
    // ============================================================
    else if (mode == "ls") {
        if (!is_dir) return "错误: 这不是一个目录";

        if (!VFS::getInstance().check_resource_access(vfs_path)) {
            return "权限不足: 无法列出该目录";
        }

        auto items = VFS::getInstance().list_directory(vfs_path);
        state.path = vfs_path;
        state.cmd = "open";
        
        if (items.empty()) return "";
        
        // 🚀 核心修复：使用 vector 收集所有真正有效的非空条目
        std::vector<std::string> valid_items;
        for (const auto& item : items) {
            if (item.empty()) continue;
            // 防止纯粹的 ANSI 转义码或纯换行符混入
            if (item == "\n" || item == "\r" || item == "\r\n") continue;
            valid_items.push_back(item);
        }

        if (valid_items.empty()) return "";

        std::string result;
        for (size_t i = 0; i < valid_items.size(); ++i) {
            result += valid_items[i];
            // 🚀 修复2：只在两个有效项之间加换行，最后一项后面绝对不加
            if (i + 1 < valid_items.size()) result += "\n";
        }
        
        state.data = result;
        return result;
    }

    // ============================================================
    // 🚀 cd 模式
    // ============================================================
    else if (mode == "cd") {
        if (!is_dir) {
            return "错误: 目标不是一个目录";
        }

        if (!VFS::getInstance().check_resource_access(vfs_path)) {
            return "权限不足: 无法进入该目录";
        }

        VFS::getInstance().set_cwd(vfs_path);
        state.cwd = VFS::getInstance().get_cwd();
        state.path = state.cwd;
        state.cmd = "open";
        return "";
    }

    // ============================================================
    // 🚀 info 模式
    // ============================================================
    else if (mode == "info") {
        if (!VFS::getInstance().check_resource_access(vfs_path)) {
            return "权限不足: 无法查看该资源的元数据";
        }

        std::string out;
        out += "名称: " + display_path(vfs_path) + "\n";

        // 树形分类
        std::string tree_type;
        if (vfs_path == "/") {
            tree_type = "干目录";
        } else if (is_letter_drive_root(vfs_path)) {
            tree_type = "支目录";
        } else if (is_dir) {
            tree_type = "杈目录";
        } else {
            tree_type = "叶目录";
        }
        out += "类型: " + tree_type + "\n";

        if (is_dir) {
            uint64_t dir_size = VFS::getInstance().get_dir_size(vfs_path);
            out += "大小: " + std::to_string(dir_size) + " 字节\n";
            std::vector<std::string> items = VFS::getInstance().list_directory(vfs_path);
            out += "包含: " + std::to_string(items.size()) + " 项\n";
        } else {
            std::string content = VFS::getInstance().read_file(vfs_path);
            out += "大小: " + std::to_string(content.size()) + " 字节\n";
        }

        ResourceInfo ri = VFS::getInstance().get_resource_info_dynamic(vfs_path);

        std::string tag = DiskFileSystem::get_resource_tag(
                              VFS::getInstance().get_disk_driver(), vfs_path);

        std::string owner_str;
        if (tag == "R0") {
            owner_str = "R0";
        } else if (tag == "PATH") {
            owner_str = "-";
        } else if (ri.owner_id == -1) {
            owner_str = "-";
        } else {
            owner_str = owner_id_to_role_name(ri.owner_id);
        }
        out += "归属: " + owner_str + "\n";

        std::string rt;
        switch (ri.type) {
            case ResourceType::PUBLIC:  rt = "公共资源"; break;
            case ResourceType::PRIVATE: rt = "私有资源"; break;
            case ResourceType::SPECIAL: rt = "特殊资源"; break;
            case ResourceType::GROUP:   rt = "组资源";   break;
            case ResourceType::SYSTEM:  rt = "系统资源"; break;
            default:                    rt = "公共资源"; break;
        }
        out += "资源类型: " + rt + "\n";
        out += "归属域: " + std::to_string(ri.domain_id) + "\n";

        std::string perms;
        perms += (ri.perms & PERM_READ)  ? "读" : "-";
        perms += (ri.perms & PERM_WRITE) ? "写" : "-";
        perms += (ri.perms & PERM_MOD)   ? "改" : "-";
        perms += (ri.perms & PERM_DEL)   ? "删" : "-";
        perms += (ri.perms & PERM_CLEAR) ? "清" : "-";
        perms += (ri.perms & PERM_LIST)  ? "查" : "-";
        perms += (ri.perms & PERM_EXEC)  ? "执" : "-";
        out += "权限: " + perms + "\n";

        state.path = vfs_path;
        state.cmd = "open";
        if (!out.empty() && out.back() == '\n') out.pop_back();
        return out;
    }

    // ============================================================
    // 🚀 save 模式
    // ============================================================
    else if (mode == "save") {
        if (!VFS::getInstance().check_resource_access(vfs_path)) {
            return "权限不足: 无法保存该文件";
        }

        auto existing = VFS::getInstance().get_node(vfs_path);
        std::string res;
        if (existing) {
            res = VFS::getInstance().write_file(vfs_path, state.data);
        } else {
            res = VFS::getInstance().create_file(vfs_path, state.data);
        }

        if (res.empty()) {
            state.path = vfs_path;
            state.cmd = "open";
            return "已保存";
        }
        return "保存失败: " + res;
    }

    return "未知模式: " + mode;
}

// 🚀 三参数重载（兼容老调用，回调默认为 nullptr）
std::string open_cmd(DCLState& state, const std::string& raw_path, const std::string& raw_mode) {
    std::vector<std::string> args;
    if (!raw_path.empty()) args.push_back(raw_path);
    if (!raw_mode.empty() && raw_mode != "auto") args.push_back(raw_mode);
    return open_cmd(state, args, nullptr);
}