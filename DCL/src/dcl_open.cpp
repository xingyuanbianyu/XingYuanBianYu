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
#include <sstream>

// =========================================================================
// 🚀 前置声明
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
extern std::string drt_get_name_by_uid(int uid);

// =========================================================================
// 🚀 归属映射与盘符解析
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

static bool is_letter_drive_root(const std::string& vfs_path) {
    if (vfs_path.size() == 4 && vfs_path[0] == '/' &&
        std::isalpha((unsigned char)vfs_path[1]) &&
        vfs_path[2] == ':' && vfs_path[3] == '/') return true;
    return false;
}

static std::string display_path(const std::string& vfs_path) {
    if (is_letter_drive_root(vfs_path)) return vfs_path.substr(1);
    if (vfs_path.size() > 4 && vfs_path[0] == '/' &&
        std::isalpha((unsigned char)vfs_path[1]) &&
        vfs_path[2] == ':' && vfs_path[3] == '/') return vfs_path.substr(1);
    return vfs_path;
}

static std::string resolve_path_prefix(const std::string& raw) {
    if (raw.empty()) return raw;
    if (raw == "/") return "/";
    std::string cwd = VFS::getInstance().get_cwd();
    if (cwd.empty()) cwd = "/";
    if (raw == "~") return cwd;
    if (raw == ".") return cwd;

    if (raw.size() >= 2 && raw[0] == '~' && raw[1] == '/') {
        int uid = VFS::getInstance().get_user_id();
        std::string uname = drt_get_name_by_uid(uid);
        std::string home_dir = uname.empty() ? "/" : ("/home/" + uname);
        if (home_dir.back() == '/') return home_dir + raw.substr(2);
        return home_dir + raw.substr(1);
    }
    if (raw.size() >= 2 && raw[0] == '.' && raw[1] == '/') {
        if (cwd.back() == '/') return cwd + raw.substr(2);
        return cwd + raw.substr(1);
    }
    return raw;
}

// =========================================================================
// 🚀 open 唯一入口
// =========================================================================
std::string open_cmd(DCLState& state, const std::vector<std::string>& raw_args,
                     std::function<void(const std::string&)> xyn_executor) {
    
    if (raw_args.empty()) return "用法: open <路径> [模式]";

    // ============================================================
    // 🚀 功能1：<< 追加写入模式（内联文本转义引擎）
    // ============================================================
    int redir_idx = -1;
    for (size_t i = 0; i < raw_args.size(); ++i) {
        if (raw_args[i] == "<<") { redir_idx = i; break; }
    }

    if (redir_idx != -1) {
        if (redir_idx == 0) return "错误: << 前面缺少文件路径";
        std::string target_path = raw_args[0];
        std::string raw_text;
        for (size_t i = redir_idx + 1; i < raw_args.size(); ++i) {
            if (!raw_text.empty()) raw_text += " ";
            raw_text += raw_args[i];
        }

        std::string final_text = "";
        for (size_t i = 0; i < raw_text.size(); ++i) {
            char c = raw_text[i];
            if (c == '"' || c == ',') continue;
            if (c == '\\' && i + 1 < raw_text.size()) {
                char next = raw_text[i + 1];
                if (next == 'n') { final_text += '\n'; i++; }
                else if (next == 'r') { final_text += "    "; i++; }
                else if (next == 't') { final_text += '\t'; i++; }
                else if (next == 's') { final_text += "[定位行]"; i++; }
                else if (next == 'h') { final_text += "[定位列]"; i++; }
                else if (next == 'u') { final_text += "[搜索]"; i++; }
                else if (next == '\\') { final_text += '\\'; i++; }
                else { final_text += ' '; i++; }
            } else if (c == '\\') {
                final_text += ' ';
            } else {
                final_text += c;
            }
        }

        std::string vfs_path = VFS::getInstance().normalize_path(resolve_path_prefix(target_path));
        if (!VFS::getInstance().check_resource_access(vfs_path)) return "权限不足: 无法写入该文件";

        auto node = VFS::getInstance().get_node(vfs_path);
        if (node && std::dynamic_pointer_cast<Directory>(node)) return "错误: 目标是一个目录，无法追加内容";
        if (!node) VFS::getInstance().create_file(vfs_path, "");

        std::string old_content = VFS::getInstance().read_file(vfs_path);
        if (old_content == "错误: 找不到该文件" || old_content == "错误: 这是一个目录") old_content = "";
        
        std::string res = VFS::getInstance().write_file(vfs_path, old_content + final_text);
        if (!res.empty()) return "写入失败: " + res;

        state.path = vfs_path; state.cmd = "open";
        return "已成功追加 " + std::to_string(final_text.size()) + " 字节。";
    }

    // ============================================================
    // 🚀 功能2：按行/列精准读取模式 (路径中以 :/ 分隔)
    // 语法: open path:/10 或 open path:/10:5 或 open path:/10-20
    // ============================================================
    std::string check_path = raw_args[0];
    size_t spec_pos = check_path.find(":/");
    
    if (spec_pos != std::string::npos) {
        std::string real_file = check_path.substr(0, spec_pos);
        std::string spec = check_path.substr(spec_pos + 2); // 获取 :/ 后面的数字

        std::string vfs_path = VFS::getInstance().normalize_path(resolve_path_prefix(real_file));
        auto node = VFS::getInstance().get_node(vfs_path);
        
        if (!node || std::dynamic_pointer_cast<Directory>(node)) {
            return "错误: 找不到文件或目标为目录 (" + real_file + ")";
        }

        if (!VFS::getInstance().check_resource_access(vfs_path)) return "权限不足: 无法读取";

        std::string content = VFS::getInstance().read_file(vfs_path);
        std::stringstream ss(content);
        std::string line_str;
        std::vector<std::string> lines;
        while (std::getline(ss, line_str)) lines.push_back(line_str);

        try {
            size_t dash_pos = spec.find('-');
            size_t colon_pos = spec.find(':');

            // 1. 提取指定范围行："/10-20"
            if (dash_pos != std::string::npos) {
                int start = std::stoi(spec.substr(0, dash_pos));
                int end = std::stoi(spec.substr(dash_pos + 1));
                if (start < 1) start = 1;
                if (end > (int)lines.size()) end = lines.size();
                
                std::string result = "";
                for (int i = start; i <= end; ++i) {
                    result += lines[i - 1] + "\n";
                }
                state.data = result; state.path = vfs_path; state.cmd = "open";
                return result;
            }
            // 2. 提取指定行和列："/10:5"
            else if (colon_pos != std::string::npos) {
                int line_num = std::stoi(spec.substr(0, colon_pos));
                int col_num = std::stoi(spec.substr(colon_pos + 1));
                
                if (line_num < 1 || line_num > (int)lines.size()) return "错误: 行号越界";
                std::string& target_line = lines[line_num - 1];
                
                if (col_num < 1) col_num = 1;
                if (col_num > (int)target_line.size()) return ""; // 列号越界返回空

                std::string result = target_line.substr(col_num - 1);
                state.data = result; state.path = vfs_path; state.cmd = "open";
                return result;
            }
            // 3. 提取单行："/10"
            else {
                int line_num = std::stoi(spec);
                if (line_num < 1 || line_num > (int)lines.size()) return "错误: 行号越界";
                
                std::string result = lines[line_num - 1];
                state.data = result; state.path = vfs_path; state.cmd = "open";
                return result;
            }
        } catch (...) {
            return "错误: 行列定位符格式不正确 (示例: test.txt:/10 或 test.txt:/10:5)";
        }
    }

    // ============================================================
    // 正常模式解析
    // ============================================================
    static const std::set<std::string> known_modes = {
        "nano", "exec", "del", "ls", "cd", "read", "info", "save", "auto"
    };

    std::string raw_path = raw_args[0];
    std::string raw_mode = "auto";
    std::vector<std::string> extra_args; 

    if (raw_args.size() > 1 && known_modes.count(raw_args.back())) {
        raw_mode = raw_args.back();
        for (size_t i = 1; i + 1 < raw_args.size(); ++i) {
            extra_args.push_back(raw_args[i]);
        }
    } else {
        for (size_t i = 1; i < raw_args.size(); ++i) {
            raw_path += " " + raw_args[i];
        }
    }

    if (raw_path.empty()) return "用法: open <路径> [模式]";

    std::string path_to_normalize = resolve_path_prefix(raw_path);

    if (path_to_normalize.find('\n') != std::string::npos ||
        path_to_normalize.find('\r') != std::string::npos) {
        return "错误: 路径或文件名中检测到换行符！请检查是否误将脚本内容作为文件名输入。";
    }

    if (raw_mode == "exec") {
        std::vector<std::string> exec_args;
        exec_args.push_back(path_to_normalize); 
        for (const auto& a : extra_args) {
            exec_args.push_back(a);
        }
        return exec_cmd(state, exec_args, xyn_executor);
    }

    std::string vfs_path = VFS::getInstance().normalize_path(path_to_normalize);

    if (raw_mode == "nano") {
        if (!VFS::getInstance().check_resource_access(vfs_path)) return "权限不足: 无法编辑该文件";
        size_t last_slash = vfs_path.find_last_of('/');
        std::string filename_only = (last_slash == std::string::npos) ? vfs_path : vfs_path.substr(last_slash + 1);
        if (filename_only.empty() || filename_only == "/") return "错误: 无效的文件名，不能为空";
        if (!VFS::getInstance().get_node(vfs_path)) {
            std::string create_res = VFS::getInstance().create_file(vfs_path, "");
            if (!create_res.empty()) return "错误: 无法创建文件 (" + vfs_path + "): " + create_res;
        }
        return nano_cmd(state, vfs_path);
    }

    if (raw_mode == "del") {
        std::vector<std::string> del_args = { vfs_path };
        return del_cmd(state, del_args, true); 
    }

    auto vfs_node = VFS::getInstance().get_node(vfs_path);
    if (!vfs_node) return "错误: 找不到 (" + vfs_path + ")";

    bool is_dir = (std::dynamic_pointer_cast<Directory>(vfs_node) != nullptr);
    std::string mode = raw_mode;
    if (mode == "auto") mode = is_dir ? "cd" : "read";

    if (mode == "read") {
        if (is_dir) return "错误: 这是一个目录，无法作为文件读取";
        if (!VFS::getInstance().check_resource_access(vfs_path)) return "权限不足: 无法读取该文件";
        std::string content = VFS::getInstance().read_file(vfs_path);
        state.data = content; state.path = vfs_path; state.cmd = "open";
        return content;
    }

    else if (mode == "ls") {
        if (!is_dir) return "错误: 这不是一个目录";
        if (!VFS::getInstance().check_resource_access(vfs_path)) return "权限不足: 无法列出该目录";
        auto items = VFS::getInstance().list_directory(vfs_path);
        state.path = vfs_path; state.cmd = "open";
        if (items.empty()) return "";
        std::vector<std::string> valid_items;
        for (const auto& item : items) {
            if (item.empty()) continue;
            if (item == "\n" || item == "\r" || item == "\r\n") continue;
            valid_items.push_back(item);
        }
        if (valid_items.empty()) return "";
        std::string result;
        for (size_t i = 0; i < valid_items.size(); ++i) {
            result += valid_items[i];
            if (i + 1 < valid_items.size()) result += "\n";
        }
        state.data = result; return result;
    }

    else if (mode == "cd") {
        if (!is_dir) return "错误: 目标不是一个目录";
        if (!VFS::getInstance().check_resource_access(vfs_path)) return "权限不足: 无法进入该目录";
        VFS::getInstance().set_cwd(vfs_path);
        state.cwd = VFS::getInstance().get_cwd(); state.path = state.cwd; state.cmd = "open";
        return "";
    }

    else if (mode == "info") {
        if (!VFS::getInstance().check_resource_access(vfs_path)) return "权限不足: 无法查看该资源的元数据";
        std::string out;
        out += "名称: " + display_path(vfs_path) + "\n";
        std::string tree_type;
        if (vfs_path == "/") tree_type = "干目录";
        else if (is_letter_drive_root(vfs_path)) tree_type = "支目录";
        else if (is_dir) tree_type = "杈目录";
        else tree_type = "叶目录";
        out += "类型: " + tree_type + "\n";
        if (is_dir) {
            uint64_t dir_size = VFS::getInstance().get_dir_size(vfs_path);
            out += "大小: " + std::to_string(dir_size) + " 字节\n";
            std::vector<std::string> items = VFS::getInstance().list_directory(vfs_path);
            out += "包含: " + std::to_string(items.size()) + " 项\n";
        } else {
            out += "大小: " + std::to_string(vfs_node->size) + " 字节\n";
        }
        ResourceInfo ri = VFS::getInstance().get_resource_info_dynamic(vfs_path);
        std::string owner_str = "-";
        if (ri.owner_id != -1) owner_str = owner_id_to_role_name(ri.owner_id);
        out += "归属: " + owner_str + "\n";
        std::string rt;
        switch (ri.type) {
            case ResourceType::PUBLIC:  rt = "公共资源"; break;
            case ResourceType::PRIVATE: rt = "私有资源"; break;
            case ResourceType::SPECIAL: rt = "特殊资源"; break;
            case ResourceType::GROUP:   rt = "组资源";   break;
            case ResourceType::SYSTEM:  rt = "系统资源"; break;
            default:                    rt = "未知资源"; break;
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
        state.path = vfs_path; state.cmd = "open";
        if (!out.empty() && out.back() == '\n') out.pop_back();
        return out;
    }

    else if (mode == "save") {
        if (!VFS::getInstance().check_resource_access(vfs_path)) return "权限不足: 无法保存该文件";
        auto existing = VFS::getInstance().get_node(vfs_path);
        std::string res;
        if (existing) res = VFS::getInstance().write_file(vfs_path, state.data);
        else res = VFS::getInstance().create_file(vfs_path, state.data);
        if (res.empty()) {
            state.path = vfs_path; state.cmd = "open";
            return "已保存";
        }
        return "保存失败: " + res;
    }

    return "未知模式: " + mode;
}

std::string open_cmd(DCLState& state, const std::string& raw_path, const std::string& raw_mode) {
    std::vector<std::string> args;
    if (!raw_path.empty()) args.push_back(raw_path);
    if (!raw_mode.empty() && raw_mode != "auto") args.push_back(raw_mode);
    return open_cmd(state, args, nullptr);
}