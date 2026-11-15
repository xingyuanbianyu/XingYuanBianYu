#include "../include/dcl_del.h"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include "../../VFS/include/vfs.h"

// ============================================================
// 🚀 ~ 展开
// ============================================================
static std::string resolve_tilde_del(const std::string& raw) {
    if (raw.empty() || raw[0] != '~') return raw;
    std::string cwd = VFS::getInstance().get_cwd();
    if (cwd.empty()) cwd = "/";
    if (raw == "~") return cwd;
    if (raw.size() >= 2 && raw[1] == '/') {
        if (cwd.back() == '/') return cwd + raw.substr(2);
        return cwd + raw.substr(1);
    }
    return raw;
}

// ============================================================
// 🚀 去除首尾空白
// ============================================================
static std::string trim_str(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, (last - first + 1));
}

// ============================================================
// 🚀 通配符匹配算法
// ============================================================
static bool wildcard_match(const std::string& str, const std::string& pattern) {
    size_t s = 0, p = 0, match = 0, starIdx = std::string::npos;
    while (s < str.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == str[s])) {
            s++; p++;
        } else if (p < pattern.size() && pattern[p] == '*') {
            starIdx = p;
            match = s;
            p++;
        } else if (starIdx != std::string::npos) {
            p = starIdx + 1;
            match++;
            s = match;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') p++;
    return p == pattern.size();
}

// ============================================================
// 🚀 清理名称中的颜色和前缀
// ============================================================
static std::string extract_pure_name(const std::string& raw) {
    std::string s = raw;
    // 去 ANSI 色码
    size_t pos = 0;
    while ((pos = s.find("\033[", pos)) != std::string::npos) {
        size_t end = s.find("m", pos);
        if (end != std::string::npos) s.erase(pos, end - pos + 1);
        else break;
    }
    // 去 [文件] / [目录]
    if (s.size() > 1 && s[0] == '[') {
        size_t space = s.find(' ');
        if (space != std::string::npos) s = s.substr(space + 1);
    }
    return trim_str(s);
}

// ============================================================
// 🚀 目录回退检查
// ============================================================
static void check_and_fallback_cwd(DCLState& state) {
    std::string cwd = VFS::getInstance().get_cwd();
    if (!VFS::getInstance().get_node(cwd)) {
        std::string fallback = cwd;
        for (int i = 0; i < 10; ++i) {
            size_t pos = fallback.find_last_of('/');
            if (pos == std::string::npos || pos == 0) { fallback = "/"; break; }
            fallback = fallback.substr(0, pos);
            if (VFS::getInstance().get_node(fallback)) break;
        }
        VFS::getInstance().set_cwd(fallback);
        state.cwd = VFS::getInstance().get_cwd();
    }
}

// ================= 删除核心函数 =================
// 🚀 新增 allow_absolute 参数，默认 false（直接敲 del 时只能限制在当前目录）
std::string del_cmd(DCLState& state, const std::vector<std::string>& args, bool allow_absolute) {
    if (args.empty()) return "用法: del <目标>";

    std::string raw_path;
    for (const auto& a : args) {
        if (!raw_path.empty()) raw_path += " ";
        raw_path += a;
    }

    raw_path = trim_str(raw_path);
    if (raw_path.empty()) return "用法: del <目标>";

    // ==========================================================
    // 🚀 沙盒机制：如果不允许跨环境（不是由 open 调用的）
    // ==========================================================
    if (!allow_absolute) {
        if (raw_path == "/" || raw_path == "." || raw_path == "..") {
            return "错误: 无法删除根目录或当前/上级目录";
        }
        
        if (raw_path[0] == '/') {
            // 1. 绝对路径变相对路径: /abc -> ./abc
            raw_path = "." + raw_path;
        } else if (raw_path.rfind("./", 0) != 0 && raw_path.rfind("../", 0) != 0 && raw_path[0] != '~') {
            // 2. 普通相对路径: abc -> ./abc
            raw_path = "./" + raw_path;
        }
    }

    // 展开 ~
    raw_path = trim_str(resolve_tilde_del(raw_path));

    // 如果是跨环境删除（open 调用过来的），也要拦截核心系统目录
    if (allow_absolute) {
        std::string check_path = VFS::getInstance().normalize_path(raw_path);
        if (check_path == "/" || check_path == "." || check_path == ".." || 
            check_path == "/bin" || check_path == "/xyby") {
            return "错误: 无法删除根目录或系统核心目录";
        }
    }

    // ==========================================================
    // 🚀 如果带通配符
    // ==========================================================
    if (raw_path.find('*') != std::string::npos || raw_path.find('?') != std::string::npos) {
        std::string parent_dir;
        std::string pattern;
        size_t last_slash = raw_path.find_last_of('/');

        if (last_slash == std::string::npos) {
            parent_dir = VFS::getInstance().get_cwd();
            pattern = raw_path;
        } else if (last_slash == 0) {
            parent_dir = "/";
            pattern = raw_path.substr(1);
        } else {
            parent_dir = raw_path.substr(0, last_slash);
            pattern = raw_path.substr(last_slash + 1);
        }
        pattern = trim_str(pattern);

        // 🚀 智能解析 parent_dir（处理 "./" 和 "."）
        if (parent_dir == ".") {
            parent_dir = VFS::getInstance().get_cwd();
        } else if (parent_dir.rfind("./", 0) == 0) {
            std::string tail = parent_dir.substr(2);
            std::string cwd = VFS::getInstance().get_cwd();
            if (cwd.back() == '/') parent_dir = cwd + tail;
            else parent_dir = cwd + "/" + tail;
        }
        
        // 归一化 parent_dir，确保是绝对路径
        parent_dir = VFS::getInstance().normalize_path(parent_dir);
        if (parent_dir.empty()) parent_dir = "/";

        if (!VFS::getInstance().get_node(parent_dir)) {
            return "错误: 目录不存在 (" + parent_dir + ")";
        }

        std::vector<std::string> items = VFS::getInstance().list_directory(parent_dir);
        int deleted_count = 0;
        std::string err_msgs;
        
        std::string debug_list = "";

        for (auto& raw_item : items) {
            std::string name = extract_pure_name(raw_item);
            
            debug_list += "  [" + name + "] (原始: " + raw_item + ")\n";

            if (name == "." || name == ".." || name.empty()) continue;

            if (wildcard_match(name, pattern)) {
                std::string full_path = parent_dir;
                if (full_path.back() != '/') full_path += "/";
                full_path += name;

                std::string res = VFS::getInstance().delete_node(full_path);
                if (res.empty()) {
                    deleted_count++;
                } else {
                    err_msgs += "  [" + name + "] 失败: " + res + "\n";
                }
            }
        }

        check_and_fallback_cwd(state);

        if (deleted_count == 0 && err_msgs.empty()) {
            return "没有找到匹配 '" + pattern + "' 的项。\n[调试] 目录 '" + parent_dir + "' 中包含以下项目：\n" + debug_list;
        }
        if (!err_msgs.empty()) {
            return "删除完毕，但有部分失败:\n" + err_msgs;
        }
        return ""; 
    }

    // ==========================================================
    // 普通精确删除
    // ==========================================================
    // 🚀 同样需要将 "./" 解析为绝对路径，确保 VFS 能准确找到
    std::string target_path = raw_path;
    if (target_path.rfind("./", 0) == 0) {
        std::string tail = target_path.substr(2);
        std::string cwd = VFS::getInstance().get_cwd();
        if (cwd.back() == '/') target_path = cwd + tail;
        else target_path = cwd + "/" + tail;
    }
    target_path = VFS::getInstance().normalize_path(target_path);

    std::string vfs_result = VFS::getInstance().delete_node(target_path);
    if (!vfs_result.empty()) return vfs_result;
    
    check_and_fallback_cwd(state);
    return ""; 
}

// 🚀 保留原有单参数调用接口作为重载，默认禁止绝对路径
std::string del_cmd(DCLState& state, const std::vector<std::string>& args) {
    return del_cmd(state, args, false);
}