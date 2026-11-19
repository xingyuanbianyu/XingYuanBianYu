#include "../include/dcl_exec.h"
#include "../include/dcl_open.h"
#include "../../VFS/include/vfs.h"
#include "../lib/dcl_cmd_registry.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <string>
#include <map>
#include <filesystem>
#include <system_error>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <functional>
#include "../include/dcl_kill.h"
#include "../../APP/include/dcl_run.h"

// ==========================================================
// 【跨平台执行所需】
// ==========================================================
#ifndef _WIN32
  #include <unistd.h>
  #include <sys/wait.h>
#endif

// ==========================================================
// 【核心对接】
// ==========================================================
extern int execute_xy_code(const std::string& code);
extern std::string run_private_script(const std::string& script_content, const std::string& vfs_path, int v_pid);
extern int decode_and_run_memory(const std::string& raw_content, const std::string& name = "", int io_fd = -1);

// 🚀 对接完整的 .xyn 批处理解释器
void load_xyn_to_cli(const std::string& vfs_path,
                     const std::vector<std::string>& script_args,
                     const std::function<void(const std::string&)>& cli_executor);

// ==========================================================
// 🚀 扩展名别名表（内存缓存）
// ==========================================================
static std::map<std::string, std::vector<std::string>> g_exec_alias;
static bool g_exec_alias_loaded = false;
static std::string g_alias_load_msg = "未尝试加载";
static std::string g_alias_last_content = "";

static void load_exec_alias(bool force_reload = false) {
    const std::string cfg_path = "/xyby/data/exec.xyi";

    if (g_exec_alias_loaded && !force_reload) return;
    g_exec_alias_loaded = true;

    std::string content;

    if (VFS::getInstance().get_node(cfg_path)) {
        content = VFS::getInstance().read_file(cfg_path);
    }

    if (content.empty()) {
        if (!VFS::getInstance().get_node("/xyby")) {
            VFS::getInstance().create_directory("/xyby");
        }
        if (!VFS::getInstance().get_node("/xyby/data")) {
            VFS::getInstance().create_directory("/xyby/data");
        }

        std::string default_cfg =
            "[exec]\n"
            "js   node\n"
            "py   python|python3\n"
            "sh   bash|sh\n"
            "rb   ruby\n"
            "lua  lua5.4|lua\n"
            "exe  wine\n";

        VFS::getInstance().create_file(cfg_path, default_cfg);
        content = default_cfg;
    }

    if (content.empty()) {
        g_alias_load_msg = "失败: " + cfg_path + " 内容为空";
        return;
    }

    if (!force_reload && content == g_alias_last_content) return;
    
    g_alias_last_content = content;
    g_exec_alias.clear();

    std::stringstream ifs(content);
    std::string line;
    while (std::getline(ifs, line)) {
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());

        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t");
        line = line.substr(start, end - start + 1);

        if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') continue;

        size_t sep = line.find_first_of(" \t");
        if (sep == std::string::npos) continue;

        std::string key = line.substr(0, sep);
        std::string vals = line.substr(sep + 1);

        if (!key.empty() && key[0] == '.') key = key.substr(1);

        std::vector<std::string> aliases;
        std::stringstream ss(vals);
        std::string token;
        while (std::getline(ss, token, '|')) {
            std::stringstream ss2(token);
            std::string tok2;
            while (ss2 >> tok2) {
                if (!tok2.empty() && tok2.back() == '\r') tok2.pop_back();
                aliases.push_back(tok2);
            }
        }

        if (!key.empty() && !aliases.empty()) {
            g_exec_alias[key] = aliases;
        }
    }

    g_alias_load_msg = "成功加载 " + cfg_path + " (条目: " + std::to_string(g_exec_alias.size()) + ")";
}

// ==========================================================
// 工具函数
// ==========================================================
std::string get_extension(const std::string& path) {
    size_t dot_pos = path.find_last_of('.');
    size_t slash_pos = path.find_last_of('/');
    if (dot_pos == std::string::npos ||
        (slash_pos != std::string::npos && dot_pos < slash_pos)) return "";
    std::string ext = path.substr(dot_pos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

std::string clean_filename(const std::string& raw_name) {
    std::string name = raw_name;
    if (name.find("[文件] ") == 0) name = name.substr(7);
    if (name.find("[目录] ") == 0) name = name.substr(7);
    return name;
}

bool is_binary_content(const std::string& content) {
    if (content.size() < 4) return false;
    unsigned char c0 = (unsigned char)content[0];
    unsigned char c1 = (unsigned char)content[1];
    if (c0 == 0x7F && c1 == 'E') return true;
    if (c0 == 'M' && c1 == 'Z') return true;
    return false;
}

// ==========================================================
// 落盘辅助函数
// ==========================================================
static std::string ensure_runtime_dir() { 
    std::error_code ec;
    std::filesystem::create_directories("runtime/exec", ec);
    return "runtime/exec"; 
}

static std::string vfs_to_cache_path(const std::string& vfs_path) {
    std::string norm = VFS::getInstance().normalize_path(vfs_path);
    std::string clean;
    for (char c : norm) { if (c == '/') clean += '_'; else clean += c; }
    while (!clean.empty() && clean.front() == '_') clean.erase(clean.begin());
    return ensure_runtime_dir() + "/" + (clean.empty() ? "unnamed" : clean);
}

static bool dump_to_file(const std::string& content, const std::string& path) {
    std::ofstream ofs(path, std::ios::binary);
    return ofs.is_open() && (ofs.write(content.data(), content.size()), ofs.good());
}

static void make_executable(const std::string& path) {
    std::error_code ec;
    std::filesystem::permissions(path,
        std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add, ec);
}

// ==========================================================
// 🚀 exec 命令主入口（接收 vector 参数并全部走内核调度）
// ==========================================================
std::string exec_cmd(DCLState& state, const std::vector<std::string>& cmd_args,
                     std::function<void(const std::string&)> xyn_executor) {
    
    load_exec_alias();

    std::string target_path;
    std::vector<std::string> script_args;

    // 1. 完美解包参数
    if (cmd_args.empty()) {
        if (state.path.empty()) return "错误: exec 需要先 open 一个目标";
        target_path = state.path;
    } else {
        target_path = cmd_args[0];
        for (size_t i = 1; i < cmd_args.size(); ++i) {
            script_args.push_back(cmd_args[i]);
        }
    }

    std::string vfs_target = VFS::getInstance().normalize_path(target_path);
    auto node = VFS::getInstance().get_node(vfs_target);
    if (!node) return "错误: 找不到 " + vfs_target + "，exec 无法执行";

    // 目录扫描
    if (std::dynamic_pointer_cast<Directory>(node)) {
        std::vector<std::string> children = VFS::getInstance().list_directory(vfs_target);
        std::string candidate;
        for (const auto& raw_child : children) {
            std::string child_name = clean_filename(raw_child);
            std::string ext = get_extension(child_name);
            if (ext == ".xy" || ext == ".xya" || ext == ".xye" || ext == ".xyn" || ext == ".xyv") {
                candidate = vfs_target;
                if (candidate.back() != '/') candidate += '/';
                candidate += child_name;
                break;
            }
        }
        if (candidate.empty()) {
            for (const auto& raw_child : children) {
                std::string child_name = clean_filename(raw_child);
                std::string ext = get_extension(child_name);
                if (ext == ".exe" || ext == ".sh" || ext == ".py" || ext == ".js") {
                    candidate = vfs_target;
                    if (candidate.back() != '/') candidate += '/';
                    candidate += child_name;
                    break;
                }
            }
        }
        if (candidate.empty()) return "错误: 目录下没有可执行文件";
        vfs_target = candidate;

        node = VFS::getInstance().get_node(vfs_target);
        if (!node) return "错误: 找不到 " + vfs_target;
    }

    std::string ext = get_extension(vfs_target);

    // 权限校验
    if (!VFS::getInstance().check_perm(vfs_target, PERM_EXEC)) {
        return "权限不足: 无法执行该文件 (" + vfs_target + ")";
    }

    // ==========================================================
    // 🚀 1. .xyn 脚本：直接扔给批处理引擎，并把参数传进去
    // ==========================================================
    if (ext == ".xyn") {
        if (!xyn_executor) {
            return "错误: .xyn 需要 CLI 执行器回调（当前未注册）";
        }

        load_xyn_to_cli(vfs_target, script_args, xyn_executor);

        state.cmd = "exec";
        state.path = vfs_target;
        return "";
    }

    // 读取内容
    std::string content = VFS::getInstance().read_file(vfs_target);

    // .xy 拦截
    if (ext == ".xyb" || ext == ".xyd") {
        return "错误: 被动模块文件无法主动执行";
    }
    if (ext == ".xya" || ext == ".xye") {
        int rc = decode_and_run_memory(content, vfs_target);
        return rc == 0 ? "" : "错误: 字节码执行失败，返回码 " + std::to_string(rc);
    }
    
    // ==========================================================
    // 🚀 2. .xy 脚本：直接内部解释执行，避免 fork 外部进程导致锁冲突
    // ==========================================================
    if (ext == ".xy" || ext == ".xyv") {
        std::string full_code;
        
        // 把参数当作全局变量注入进去
        for (size_t k = 0; k < script_args.size(); ++k) {
            full_code += "arg" + std::to_string(k + 1) + " = \"";
            // 防止参数里出现引号导致转义失败
            for (char c : script_args[k]) {
                if (c == '"' || c == '\\') full_code += '\\';
                full_code += c;
            }
            full_code += "\"\n";
        }
        
        full_code += content;

        try {
            // 🚀 直接走内部解释器执行
            execute_xy_code(full_code);
            state.cmd = "exec";
            state.path = vfs_target;
            return "";
        } catch (const std::exception& e) {
            return "错误: 脚本执行异常: " + std::string(e.what());
        } catch (...) {
            return "错误: 脚本执行发生未知异常";
        }
    }

    // ==========================================================
    // 🚀 3. 别名表 / ELF / PE：使用内核调度器拉起，自动进表
    // ==========================================================
    auto& registry = CommandRegistry::getInstance();
    std::string host_path = vfs_to_cache_path(vfs_target);

    std::string ext_key = (ext.size() > 1) ? ext.substr(1) : "";
    std::string runner_path;

    if (!ext_key.empty()) {
        auto it = g_exec_alias.find(ext_key);
        if (it != g_exec_alias.end()) {
            for (const auto& alias : it->second) {
                runner_path = registry.find(alias);
                if (!runner_path.empty()) break;
            }
        }
        if (runner_path.empty()) {
            runner_path = registry.find(ext_key);
        }
    }

    // 如果找到了解释器（如 python/node）
    if (!runner_path.empty()) {
        if (!dump_to_file(content, host_path)) return "错误: 无法创建执行缓存 " + host_path;
        make_executable(host_path);
        std::string abs_path = std::filesystem::absolute(host_path).string();
        
        std::vector<std::string> spawn_args;
        spawn_args.push_back(runner_path); // 解释器
        spawn_args.push_back(abs_path);    // 脚本
        for (const auto& a : script_args) {
            spawn_args.push_back(a);       // 附加参数
        }

        // 🚀 统一走内核调度
        return spawn_process(spawn_args, runner_path, false);
    }

    // 二进制文件（ELF/PE）
    if (is_binary_content(content)) {
        if (!dump_to_file(content, host_path)) return "错误: 无法创建执行缓存";
        make_executable(host_path);
        std::string abs_path = std::filesystem::absolute(host_path).string();
        
        std::vector<std::string> spawn_args;
        
        if ((unsigned char)content[0] == 0x7F) {
            // Linux ELF
            spawn_args.push_back(abs_path);
            for (const auto& a : script_args) spawn_args.push_back(a);
            
            // 🚀 统一走内核调度
            return spawn_process(spawn_args, abs_path, false);
        } else {
            // Windows PE
            std::string wine_path = registry.find("wine");
            if (wine_path.empty()) return "错误: 未找到 wine 解释器，无法执行 PE 文件";
            std::string exe_path = host_path + ".exe";
            if (!dump_to_file(content, exe_path)) return "错误: 无法创建 exe 临时文件";
            
            spawn_args.push_back(wine_path);
            spawn_args.push_back(exe_path);
            for (const auto& a : script_args) spawn_args.push_back(a);
            
            // 🚀 统一走内核调度
            std::string res = spawn_process(spawn_args, wine_path, false);
            std::filesystem::remove(exe_path);
            return res;
        }
    }

    std::string debug_msg = " [别名表: " + g_alias_load_msg + "]";
    if (!ext_key.empty()) {
        if (g_exec_alias.find(ext_key) != g_exec_alias.end()) {
            debug_msg += " [别名表已命中, 尝试寻找:";
            for (const auto& a : g_exec_alias[ext_key]) debug_msg += " " + a;
            debug_msg += "]";
        } else {
            debug_msg += " [无别名记录]";
        }
    }

    return "错误: 无法运行该文件 (" + (ext.empty() ? "无扩展名" : "扩展名 " + ext) + " 无对应运行程序，且无 ELF/PE 文件头)" + debug_msg;
}