// ============================================================
// lib/main_helpers.inc —— main.cpp 的辅助函数集合
// ============================================================
#pragma once

#include <termios.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <cstring>
#include <pty.h>       
#include <sys/ioctl.h> 
#include <poll.h>
#include <fcntl.h>
#include <cstdio>

std::string new_prompt = "";
std::string cwd = "/";

// 🚀 引入 nano 交互所用的全局变量（定义在 dcl_nano.cpp）
extern std::string g_nano_edit_content;
extern std::string g_nano_edit_result;

// 🚀 全局密码变量和密码读取函数前置声明
extern std::string g_sudo_pass;
std::string read_password_silent(const std::string& prompt);
std::string drt_get_r3_name();
extern std::string drt_get_name_by_uid(int uid); 

void kernel_process_signals();
std::string nano_cmd(DCLState& state, const std::string& vfs_path);

// ============================================================
// 🚀 远程交互输入接口
// ============================================================
int g_backend_client_fd = -1; 

std::string request_frontend_input(const std::string& prompt) {
    if (g_backend_client_fd == -1) return "None"; 
    
    std::string req = "__REQ_INPUT__:" + prompt + "\n";
    write(g_backend_client_fd, req.c_str(), req.size());
    
    char buf[4096];
    int n = read(g_backend_client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return "None";
    buf[n] = '\0';
    std::string resp(buf);
    
    if (resp.rfind("__INPUT_DATA__:", 0) == 0) {
        std::string data = resp.substr(15);
        while (!data.empty() && (data.back() == '\n' || data.back() == '\r')) data.pop_back();
        return data;
    }
    return "None";
}

// ============================================================
// 🚀 内存文件头结构（严格 24 字节）
// ============================================================
#pragma pack(push, 1)
struct MemoryHeader {
    char owner[9]     = {0};
    char version[6]   = {0};
    char magic[5]     = {0};
    uint8_t vb[3]     = {0};
    uint8_t group     = 7;

    MemoryHeader(const std::string& magic_str) {
        std::strncpy(owner,   "XingYuan", 8);
        std::strncpy(version, "1.0.0",    5);
        std::strncpy(magic,   magic_str.c_str(), 4);

        if (magic_str == "XYBH") group = 8;
        else                     group = 7;
    }

    void writeTo(std::ofstream& out, uint32_t valid_bits) const {
        out.write(owner,   9);
        out.write(version, 6);
        out.write(magic,   5);

        uint8_t vb_local[3] = {
            (uint8_t)( valid_bits        & 0xFF),
            (uint8_t)((valid_bits >>  8) & 0xFF),
            (uint8_t)((valid_bits >> 16) & 0xFF)
        };
        out.write(reinterpret_cast<const char*>(vb_local), 3);
        out.write(reinterpret_cast<const char*>(&group), 1);
    }
};
#pragma pack(pop)

// ============================================================
// 🚀 后缀白名单
// ============================================================
static bool has_allowed_ext(const std::string& path) {
    static const std::vector<std::string> exts =
        {".xy", ".xyv", ".xya", ".xye", ".xyk", ".xym", ".xyh", ".xyn"};
    for (const auto& ext : exts) {
        if (path.size() >= ext.size() &&
            path.compare(path.size() - ext.size(), ext.size(), ext) == 0)
            return true;
    }
    return false;
}

// ============================================================
// 🚀 去掉 ANSI 色码
// ============================================================
static std::string strip_ansi(const std::string& s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
            size_t j = i + 2;
            while (j < s.size() && s[j] != 'm') j++;
            if (j < s.size()) j++;
            i = j;
        } else {
            out += s[i++];
        }
    }
    return out;
}

// ============================================================
// 🚀 去掉 "[文件] " / "[目录] " 前缀
// ============================================================
static std::string strip_list_prefix(const std::string& s) {
    size_t sp = s.find(' ');
    if (sp != std::string::npos && s[0] == '[') {
        return s.substr(sp + 1);
    }
    return s;
}

// ============================================================
// 🚀 统一路径前缀解析
// ============================================================
static std::string resolve_vfs_path_prefix(const std::string& raw) {
    if (raw.empty()) return raw;
    if (raw == "/") return "/";
    
    std::string cwd = VFS::getInstance().get_cwd();
    if (cwd.empty()) cwd = "/";

    if (raw == "~" || raw == ".") return cwd;

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

// ============================================================
// #include 预处理
// ============================================================
static const std::map<std::string, std::string>& load_include_config() {
    static std::map<std::string, std::string> config;
    static std::string cached_raw = "";
    static bool has_cache = false;

    std::string cfg_path = "/lib.xyt";
    std::string current_raw = "";

    if (VFS::getInstance().get_node(cfg_path)) {
        current_raw = VFS::getInstance().read_file(cfg_path);
    }

    if (has_cache && current_raw == cached_raw) {
        return config;
    }

    config.clear();
    cached_raw = current_raw;
    has_cache = true;

    if (current_raw.empty()) {
        return config;
    }

    std::stringstream ifs(current_raw);
    std::string line;
    bool in_include_section = false;

    while (std::getline(ifs, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            in_include_section = (line == "[include]");
            continue;
        }
        if (!in_include_section) continue;

        size_t sep = line.find_first_of(" \t=");
        if (sep == std::string::npos) continue;

        std::string name = trim(line.substr(0, sep));
        std::string path = trim(line.substr(sep + 1));
        while (!path.empty() && (path[0] == ' ' || path[0] == '\t'))
            path = path.substr(1);

        if (!name.empty() && !path.empty())
            config[name] = path;
    }

    return config;
}

std::string resolve_system_include(const std::string& name) {
    const auto& cfg = load_include_config();
    auto it = cfg.find(name);
    if (it == cfg.end()) return "";

    std::string rel = it->second;
    if (rel.empty()) return "";
    if (rel[0] == '/') return rel;

    return "/lib/" + rel;
}

std::string resolve_user_include(const std::string& rel,
                                 const std::string& base_dir) {
    if (rel.empty()) return "";
    if (rel[0] == '/') return rel;
    std::string base = base_dir.empty() ? g_user_cwd : base_dir;
    return (fs::path(base) / rel).string();
}

std::string expand_includes(const std::string& code,
                            std::set<std::string>& included_files,
                            const std::string& base_dir) {
    std::stringstream ss(code);
    std::string line;
    std::string result;

    std::string effective_base = base_dir.empty() ? g_script_dir : base_dir;

    while (std::getline(ss, line)) {
        std::string processed_line = line;

        size_t hash_pos = processed_line.find('#');
        if (hash_pos != std::string::npos) {
            size_t kw_pos = hash_pos + 1;
            while (kw_pos < processed_line.size() &&
                   isspace((unsigned char)processed_line[kw_pos])) kw_pos++;
            std::string after_hash = processed_line.substr(kw_pos);
            if (after_hash.rfind("包含", 0) == 0 ||
                after_hash.rfind("包括", 0) == 0) {
                std::string args = after_hash.substr(6);
                processed_line = processed_line.substr(0, hash_pos) + "#include" + args;
            }
        }

        std::string trimmed = trim(processed_line);

        if (trimmed.rfind("#include", 0) == 0) {
            std::string arg = trim(trimmed.substr(8));

            if (arg.size() >= 2 && arg.front() == '<' && arg.back() == '>') {
                std::string name = arg.substr(1, arg.size() - 2);
                std::string vfs_path = resolve_system_include(name);
                if (vfs_path.empty()) {
                    result += "// [ERROR] 未在 /lib.xyt 找到: " + name + "\n";
                    continue;
                }

                vfs_path = VFS::getInstance().normalize_path(vfs_path);
                std::string key = "vfs:" + vfs_path;

                if (included_files.count(key)) {
                    result += "// [WARNING] 循环包含已跳过: " + vfs_path + "\n";
                    continue;
                }

                auto node = VFS::getInstance().get_node(vfs_path);
                if (!node) {
                    result += "// [ERROR] VFS 文件不存在: " + vfs_path + "\n";
                    continue;
                }

                std::string file_content = VFS::getInstance().read_file(vfs_path);
                included_files.insert(key);

                std::string new_base = vfs_path.substr(0, vfs_path.find_last_of('/'));
                if (new_base.empty()) new_base = "/";

                result += expand_includes(file_content, included_files, new_base) + "\n";
                included_files.erase(key);
            }
            else if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"') {
                std::string rel = arg.substr(1, arg.size() - 2);
                if (rel.empty()) {
                    result += "// [ERROR] 空路径\n";
                    continue;
                }

                rel = resolve_vfs_path_prefix(rel);

                std::string vfs_path;
                if (rel[0] == '/') {
                    vfs_path = rel;
                } else {
                    std::string vb = effective_base.empty() ? "/" : effective_base;
                    vfs_path = vb + "/" + rel;
                }
                vfs_path = VFS::getInstance().normalize_path(vfs_path);

                auto vfs_node = VFS::getInstance().get_node(vfs_path);

                if (vfs_node) {
                    std::string key = "vfs:" + vfs_path;
                    if (included_files.count(key)) {
                        result += "// [WARNING] 循环包含已跳过: " + vfs_path + "\n";
                        continue;
                    }

                    std::string file_content = VFS::getInstance().read_file(vfs_path);
                    included_files.insert(key);

                    std::string new_base = vfs_path.substr(0, vfs_path.find_last_of('/'));
                    if (new_base.empty()) new_base = "/";

                    result += expand_includes(file_content, included_files, new_base) + "\n";
                    included_files.erase(key);
                }
                else {
                    std::string target_path = resolve_user_include(rel, effective_base);
                    if (target_path.empty()) {
                        result += "// [ERROR] 空路径\n";
                        continue;
                    }

                    std::error_code ec;
                    fs::path canon = fs::weakly_canonical(target_path, ec);
                    if (!ec) target_path = canon.string();

                    std::string key = "host:" + target_path;
                    if (included_files.count(key)) {
                        result += "// [WARNING] 循环包含已跳过: " + target_path + "\n";
                        continue;
                    }

                    std::ifstream ifs(target_path);
                    if (ifs.is_open()) {
                        std::stringstream buf;
                        buf << ifs.rdbuf();
                        included_files.insert(key);

                        std::string new_base = fs::path(target_path).parent_path().string();
                        result += expand_includes(buf.str(), included_files, new_base) + "\n";

                        included_files.erase(key);
                    } else {
                        result += "// [ERROR] 文件不存在: " + target_path + "\n";
                    }
                }
            }
            else {
                result += processed_line + "\n";
            }
        }
        else {
            result += processed_line + "\n";
        }
    }
    return result;
}

// ============================================================
// 🚀 命令索引内存表
// ============================================================
struct LibIndexCache {
    std::map<std::string, std::string> table; 
    std::string signature = "";               
    bool loaded = false;
};

static LibIndexCache g_lib_cache;

static std::string get_lib_dir_signature(const std::vector<std::string>& items) {
    std::string sig;
    for (const auto& item : items) {
        sig += item + "|";
    }
    return sig;
}

inline void refresh_lib_index() {
    std::string target_dir = "/bin/lib";
    if (!VFS::getInstance().get_node(target_dir)) {
        g_lib_cache.loaded = false;
        return;
    }

    std::vector<std::string> items = VFS::getInstance().list_directory(target_dir);
    std::string current_sig = get_lib_dir_signature(items);
    
    if (g_lib_cache.loaded && g_lib_cache.signature == current_sig) {
        return;
    }

    g_lib_cache.table.clear();
    for (auto& raw : items) {
        std::string name = strip_ansi(raw);
        if (name.empty()) continue;
        name = strip_list_prefix(name);
        
        if (!has_allowed_ext(name)) continue;

        std::string cmd = name;
        size_t dot = cmd.rfind('.');
        if (dot != std::string::npos) cmd = cmd.substr(0, dot);
        if (cmd.empty()) continue;

        std::string full_path = target_dir + "/" + name;
        g_lib_cache.table[cmd] = full_path;
    }
    
    g_lib_cache.signature = current_sig;
    g_lib_cache.loaded = true;

    std::string data;
    for (auto& [cmd, path] : g_lib_cache.table) {
        data += cmd + " " + path + "\n";
    }
    std::string path = "/bin/data.xyi";
    if (VFS::getInstance().get_node(path)) {
        VFS::getInstance().write_file(path, data);
    } else {
        VFS::getInstance().create_file(path, data);
    }
}

inline void print_help_index() {
    refresh_lib_index();
    
    std::cout << "\n========== 星缘编语可用命令 ==========\n";

    if (g_lib_cache.table.empty()) {
        std::cout << "(暂无内置命令，请把脚本放入 /bin/lib 目录)\n";
        std::cout << "=====================================\n\n";
        return;
    }

    for (const auto& [cmd, file] : g_lib_cache.table) {
        std::cout << "  " << cmd << "  (" << file << ")\n";
    }
    
    std::cout << "共 " << g_lib_cache.table.size() << " 条内置命令。\n";
    std::cout << "=====================================\n\n";
}

inline bool resolve_lib_command(const std::string& cmd, std::string& out_path) {
    refresh_lib_index();
    auto it = g_lib_cache.table.find(cmd);
    if (it != g_lib_cache.table.end()) {
        out_path = it->second;
        return true;
    }
    return false;
}

// ============================================================
// 用户路径解析
// ============================================================
static std::string resolve_user_path(const std::string& input) {
    if (input.empty()) return input;
    std::string path = input;

    if (path[0] == '~') {
        const char* home = getenv("HOME");
        if (home) path = std::string(home) + path.substr(1);
    }

    if (path[0] != '/') {
        if (!g_user_cwd.empty())
            path = g_user_cwd + "/" + path;
        else
            path = "./" + path;
    }

    std::error_code ec;
    fs::path p = fs::weakly_canonical(path, ec);
    if (!ec) return p.string();
    return path;
}

// ============================================================
// 🚀 backend 监听 socket 路径
// ============================================================
static const char* BACKEND_SOCK = "./.xyby.sock";

// ============================================================
// 🚀 前置声明
// ============================================================
void cli_execute_single(const std::string& cmd_line, DCLState& state, int& cli_priv);

static bool request_contains_nano(const std::string& req) {
    std::stringstream ss(req);
    std::string token;
    while (ss >> token) {
        if (token == "nano") return true;
    }
    return false;
}

static std::string backend_execute_line(const std::string& req, DCLState& state) {
    if (req == "kill" || req == "xyby kill" || req == "exit") {
        return "__FRONTEND_EXIT__";
    }
    if (req == "xyby 0 kill") {
        return "__SYS_EXIT__";
    }

    // 🚀 1. 拦截 nano，让前端接管
    if (request_contains_nano(req)) {
        std::string vfs_path = "";
        std::stringstream ss(req);
        std::string token;
        while (ss >> token) {
            if (token != "open" && token != "nano") {
                vfs_path = token;
                break;
            }
        }
        if (!vfs_path.empty() && vfs_path[0] != '/') {
            vfs_path = VFS::getInstance().normalize_path(vfs_path);
        }
        return "__REQ_NANO__:" + vfs_path;
    }

    // 🚀 2. 响应前端请求文件内容
    if (req.rfind("__GET_FILE__:", 0) == 0) {
        std::string path = req.substr(13);
        if (!VFS::getInstance().get_node(path)) {
            VFS::getInstance().create_file(path, "");
        }
        return "__FILE_DATA__:" + VFS::getInstance().read_file(path);
    }

    // 🚀 3. 响应前端保存文件请求（绝对落盘安全版）
    if (req.rfind("__SAVE_FILE__:", 0) == 0) {
        size_t nl1 = req.find('\n');
        if (nl1 != std::string::npos) {
            std::string path = req.substr(14, nl1 - 14);
            
            size_t nl2 = req.find('\n', nl1 + 1);
            if (nl2 != std::string::npos) {
                std::string len_str = req.substr(nl1 + 1, nl2 - nl1 - 1);
                uint64_t expect_len = std::stoull(len_str);
                
                std::string content = req.substr(nl2 + 1);
                while (content.size() < expect_len) {
                    char tbuf[8192];
                    ssize_t n = read(g_backend_client_fd, tbuf, sizeof(tbuf));
                    if (n <= 0) break;
                    content.append(tbuf, n);
                }
                
                if (content.size() >= expect_len) {
                    content = content.substr(0, expect_len); 
                    std::string res = VFS::getInstance().write_file(path, content);
                    if (res.empty()) {
                        return "__SAVE_OK__";
                    }
                    return "__SAVE_FAIL__:" + res;
                }
            }
        }
        return "__SAVE_FAIL__: 协议长度校验失败或传输中断";
    }

    // 普通命令执行
    std::streambuf* old_cout = std::cout.rdbuf();
    std::streambuf* old_cerr = std::cerr.rdbuf();
    std::stringstream captured;

    std::cout.rdbuf(captured.rdbuf());
    std::cerr.rdbuf(captured.rdbuf());

    int cli_priv = 0;
    cli_execute_single(req, state, cli_priv);

    kernel_process_signals();

    std::cout.rdbuf(old_cout);
    std::cerr.rdbuf(old_cerr);

    std::string out = captured.str();

    if (out.find("__SYS_EXIT__") != std::string::npos) {
        return "__SYS_EXIT__";
    }

    return "__CWD__:" + VFS::getInstance().get_cwd() + "\n" + out;
}

inline void run_backend_server(DCLState& state) {
    unlink(BACKEND_SOCK);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "[后端] socket 创建失败\n";
        return;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, BACKEND_SOCK, sizeof(addr.sun_path) - 1);

    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[后端] bind 失败\n";
        close(fd);
        return;
    }

    listen(fd, 8);
   // std::cout << "[backend] 已就绪，监听 " << BACKEND_SOCK << "\n";

    while (true) {
        int cli = accept(fd, nullptr, nullptr);
        if (cli < 0) continue;

        pid_t pid = fork();
        if (pid == 0) {
            close(fd);
            g_backend_client_fd = cli; 
            DCLState local_state = state;

            while (true) {
                std::string req;
                char buf[4096];
                ssize_t n;
                while ((n = read(cli, buf, sizeof(buf))) > 0) {
                    req.append(buf, n);
                    
                    if (req.rfind("__SUDO_PASS__:", 0) == 0) {
                        size_t first_nl = req.find('\n');
                        if (first_nl != std::string::npos) {
                            if (req.find('\n', first_nl + 1) != std::string::npos) {
                                break;
                            }
                        }
                    } else if (req.rfind("__SAVE_FILE__:", 0) == 0) {
                        size_t nl1 = req.find('\n');
                        if (nl1 != std::string::npos) {
                            size_t nl2 = req.find('\n', nl1 + 1);
                            if (nl2 != std::string::npos) {
                                std::string len_str = req.substr(nl1 + 1, nl2 - nl1 - 1);
                                try {
                                    uint64_t expect_len = std::stoull(len_str);
                                    if (req.size() - (nl2 + 1) >= expect_len) break;
                                } catch(...) {}
                            }
                        }
                    } else {
                        if (req.find('\n') != std::string::npos) {
                            break;
                        }
                    }
                }
                if (req.empty()) break;
                // 🚀 注意：这里绝对不能对 SAVE_FILE 的数据无脑 pop_back，因为末尾的换行可能是内容的一部分
                if (!req.empty() && req.back() == '\n' && req.rfind("__SAVE_FILE__:", 0) != 0) req.pop_back();

                if (req.rfind("__SUDO_PASS__:", 0) == 0) {
                    size_t nl = req.find('\n');
                    if (nl != std::string::npos) {
                        g_sudo_pass = req.substr(14, nl - 14);
                        req = req.substr(nl + 1);
                    }
                }

                if (req == "SHUTDOWN") {
                    kill(getppid(), SIGTERM);
                    close(cli);
                    _exit(0);
                }

                std::string resp = backend_execute_line(req, local_state);
                
                if (resp == "__SYS_EXIT__" || resp == "__FRONTEND_EXIT__") {
                    std::string exit_msg = resp + "\n";
                    write(cli, exit_msg.c_str(), exit_msg.size());
                    close(cli);
                    
                    if (resp == "__SYS_EXIT__") {
                        unlink(BACKEND_SOCK);
                        kill(getppid(), SIGKILL);
                        _exit(0);
                    }
                    break; 
                }

                if (!resp.empty()) {
                    write(cli, resp.c_str(), resp.size());
                }
            }
            close(cli);
            _exit(0);
        }
        close(cli);
    }

    close(fd);
}

inline int run_cli_client_mode(const std::vector<std::string>& initial_cmds) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "socket 创建失败\n";
        return 1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, BACKEND_SOCK, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "连接后端失败: " << BACKEND_SOCK << "\n";
        close(fd);
        return 1;
    }

    if (!initial_cmds.empty()) {
        std::string req;
        for (size_t i = 0; i < initial_cmds.size(); ++i) {
            if (i) req += " ";
            req += initial_cmds[i];
        }
        
        std::string send_buf = req + "\n";
        if (req.rfind("sudo ", 0) == 0) {
            std::string r3name = drt_get_r3_name();
            std::string pass = read_password_silent(r3name + " SSH:");
            send_buf = "__SUDO_PASS__:" + pass + "\n" + req + "\n";
        }
        
        write(fd, send_buf.c_str(), send_buf.size());

        char buf[8192];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            std::string resp(buf);
            if (resp.find("__SYS_EXIT__") != std::string::npos) {
                exit(0);
            }
            
            size_t cwd_pos = resp.find("__CWD__:");
            if (cwd_pos != std::string::npos) {
                size_t nl_pos = resp.find('\n', cwd_pos);
                if (nl_pos != std::string::npos) {
                    std::cout << resp.substr(nl_pos + 1);
                } else {
                    std::cout << resp;
                }
            } else {
                std::cout << resp;
            }
        }
        close(fd);
        return 0;
    }

    std::cout << "\033[1;35m欢迎使用星缘编语v1.0\033[0m\n";

    DCLKeyboard::set_context(KeyContext::MAIN);
    DCLKeyboard::set_prompt("\033[1;35m>>> \033[0m");
    std::cout << "\033[1;35m>>> \033[0m" << std::flush;

    bool is_quitting = false; 

    while (true) {
        KeyInput key = DCLKeyboard::read();
        
        std::string line;
        if (key.event == KeyEvent::TEXT || key.event == KeyEvent::COMMAND) {
            line = key.raw;
        } else {
            continue;
        }

        if (line.empty()) continue;

        DCLKeyboard::add_history(line);

        if (line == "kill" || line == "exit") {
            close(fd);
            return 0;
        }

        if (line == "xyby kill" || line == "xyby 0 kill") {
            is_quitting = true;
            usleep(200000);
        }

        std::string send_buf = line + "\n";
        if (line.rfind("sudo ", 0) == 0) {
            std::string r3name = drt_get_r3_name();
            std::string pass = read_password_silent(r3name + " SSH:");
            send_buf = "__SUDO_PASS__:" + pass + "\n" + line + "\n";
        }

        if (write(fd, send_buf.c_str(), send_buf.size()) <= 0) {
            close(fd);
            fd = -1;
        }

        char buf[8192];
        ssize_t n = -1;
        if (fd != -1) {
            n = read(fd, buf, sizeof(buf) - 1);
        }

        std::string resp = "";
        if (n > 0) {
            buf[n] = '\0';
            resp = buf;
        }

        if (resp.find("__SYS_EXIT__") != std::string::npos) {
            close(fd);
            exit(0); 
        }

        // 🚀 核心改造：接收后端的 nano 请求，在前端本地启动内存版 nano_cmd
        size_t nano_pos = resp.find("__REQ_NANO__:");
        if (nano_pos != std::string::npos) {
            std::string target_path = resp.substr(nano_pos + 13);
            while (!target_path.empty() && (target_path.back() == '\n' || target_path.back() == '\r')) {
                target_path.pop_back();
            }
            
            // 1. 向后端拿文件内容
            std::string get_req = "__GET_FILE__:" + target_path + "\n";
            write(fd, get_req.c_str(), get_req.size());
            
            char file_buf[65536] = {0};
            ssize_t fn = read(fd, file_buf, sizeof(file_buf) - 1);
            g_nano_edit_content = "";
            if (fn > 0) {
                std::string file_resp(file_buf);
                if (file_resp.rfind("__FILE_DATA__:", 0) == 0) {
                    g_nano_edit_content = file_resp.substr(14);
                }
            }
            
            // 2. 调用前端的 nano_cmd
            g_nano_edit_result = g_nano_edit_content; // 初始化
            DCLState dummy_state;
            nano_cmd(dummy_state, target_path); 
            
            // 3. 发送修改后的内容到后端（带长度校验）
            std::string save_req = "__SAVE_FILE__:" + target_path + "\n" + std::to_string(g_nano_edit_result.size()) + "\n" + g_nano_edit_result;
            
            size_t total_written = 0;
            while (total_written < save_req.size()) {
                ssize_t w = write(fd, save_req.c_str() + total_written, save_req.size() - total_written);
                if (w <= 0) break;
                total_written += w;
            }
            
            // 4. 等待后端确认
            char ack_buf[256] = {0};
            read(fd, ack_buf, sizeof(ack_buf) - 1);
            
            // 5. 彻底大扫除，恢复终端
            tcflush(STDIN_FILENO, TCIFLUSH);
            
            DCLKeyboard::set_context(KeyContext::MAIN);
            if (cwd == "/") {
                new_prompt = "\033[1;35m>>> \033[0m";
            } else {
                new_prompt = "\033[1;35m" + cwd + ">> \033[0m";
            }
            std::cout << new_prompt << std::flush;
            DCLKeyboard::set_prompt(new_prompt);
            continue;
        }

        if (is_quitting) {
            close(fd);
            exit(0);
        }

        if (n <= 0) {
            if (fd != -1) close(fd);
            
            bool reconnected = false;
            for (int retry = 0; retry < 30; ++retry) {
                usleep(100000);
                fd = socket(AF_UNIX, SOCK_STREAM, 0);
                if (fd >= 0) {
                    sockaddr_un reconnect_addr{};
                    reconnect_addr.sun_family = AF_UNIX;
                    strncpy(reconnect_addr.sun_path, BACKEND_SOCK, sizeof(reconnect_addr.sun_path) - 1);
                    if (connect(fd, (struct sockaddr*)&reconnect_addr, sizeof(reconnect_addr)) == 0) {
                        reconnected = true;
                        break;
                    }
                    close(fd);
                    fd = -1;
                }
            }
            
            if (reconnected) {
                std::string new_prompt = "\033[1;35m>>> \033[0m"; 
                std::cout << new_prompt << std::flush;
                DCLKeyboard::set_prompt(new_prompt);
                continue; 
            } else {
                exit(0);
            }
        }

        
        std::string real_output = "";

        size_t cwd_pos = resp.find("__CWD__:");
        if (cwd_pos != std::string::npos) {
            size_t nl_pos = resp.find('\n', cwd_pos);
            if (nl_pos != std::string::npos) {
                cwd = resp.substr(cwd_pos + 8, nl_pos - (cwd_pos + 8));
                real_output = resp.substr(nl_pos + 1);
            }
        } else {
            real_output = resp;
        }

        if (cwd == "/") {
            new_prompt = "\033[1;35m>>> \033[0m";
        } else {
            new_prompt = "\033[1;35m" + cwd + ">> \033[0m";
        }
        DCLKeyboard::set_prompt(new_prompt);

        if (!real_output.empty()) {
            while (!real_output.empty() && (real_output.back() == '\n' || real_output.back() == '\r')) {
                real_output.pop_back();
            }
            if (!real_output.empty()) {
                std::cout << real_output << "\n";
                std::cout << new_prompt << std::flush;
            }
        }
    }
    close(fd);
    return 0;
}

inline int run_client_mode(int argc, char* argv[]) {
    std::vector<std::string> cmds;
    for (int i = 2; i < argc; ++i) cmds.push_back(argv[i]);
    return run_cli_client_mode(cmds);
}

static bool init_exe_root() {
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) return false;

    exe_path[len] = '\0';
    std::string exe_str(exe_path);
    size_t pos = exe_str.find_last_of('/');
    if (pos == std::string::npos) return false;

    g_exe_root = exe_str.substr(0, pos);

    char cwd_buf[PATH_MAX];
    if (getcwd(cwd_buf, sizeof(cwd_buf))) {
        g_user_cwd = cwd_buf;
    } else {
        g_user_cwd = ".";
    }

    chdir(g_exe_root.c_str());
    return true;
}

// ============================================================
// 🚀 .xyn 批处理解释器
// ============================================================
struct XynContext {
    std::map<std::string, std::string> vars;

    struct IfFrame {
        int  indent;
        bool branch_taken;
        bool any_taken;
        bool in_else;
    };
    std::vector<IfFrame> if_stack;

    bool should_exec() const {
        for (auto& f : if_stack) {
            if (!f.branch_taken) return false;
        }
        return true;
    }
};

static std::string xyn_subst_vars(const std::string& s, XynContext& ctx) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '$' && i + 1 < s.size() && s[i+1] == '{') {
            size_t j = s.find('}', i + 2);
            if (j == std::string::npos) { out += s[i++]; continue; }
            std::string name = s.substr(i + 2, j - i - 2);
            auto it = ctx.vars.find(name);
            out += (it != ctx.vars.end()) ? it->second : "";
            i = j + 1;
        } else {
            out += s[i++];
        }
    }
    return out;
}

static std::string xyn_eval_value(const std::string& s, XynContext& ctx) {
    std::string t = s;
    size_t f = t.find_first_not_of(" \t");
    if (f == std::string::npos) return "";
    size_t l = t.find_last_not_of(" \t");
    t = t.substr(f, l - f + 1);

    if (t.size() >= 2 && ((t.front() == '"' && t.back() == '"') ||
                          (t.front() == '\'' && t.back() == '\''))) {
        return t.substr(1, t.size() - 2);
    }
    if (t.rfind("${", 0) == 0 && t.back() == '}') {
        std::string name = t.substr(2, t.size() - 3);
        auto it = ctx.vars.find(name);
        return (it != ctx.vars.end()) ? it->second : "";
    }
    if (!t.empty() && t[0] == '$' && t.size() > 1) {
        std::string name = t.substr(1);
        auto it = ctx.vars.find(name);
        return (it != ctx.vars.end()) ? it->second : "";
    }
    return t;
}

static bool xyn_eval_cond(const std::string& expr, XynContext& ctx,
                          const std::function<bool(const std::string&)>& exists_check) {
    std::string e = expr;
    size_t f = e.find_first_not_of(" \t");
    if (f == std::string::npos) return false;
    size_t l = e.find_last_not_of(" \t");
    e = e.substr(f, l - f + 1);
    if (e.size() >= 2 && e.front() == '(' && e.back() == ')') {
        e = e.substr(1, e.size() - 2);
        f = e.find_first_not_of(" \t");
        if (f == std::string::npos) return false;
        l = e.find_last_not_of(" \t");
        e = e.substr(f, l - f + 1);
    }

    size_t pos_eq = e.find("==");
    size_t pos_ne = e.find("!=");

    if (pos_eq != std::string::npos) {
        std::string a = xyn_eval_value(e.substr(0, pos_eq), ctx);
        std::string b = xyn_eval_value(e.substr(pos_eq + 2), ctx);
        return a == b;
    }
    if (pos_ne != std::string::npos) {
        std::string a = xyn_eval_value(e.substr(0, pos_ne), ctx);
        std::string b = xyn_eval_value(e.substr(pos_ne + 2), ctx);
        return a != b;
    }
    std::string val = xyn_eval_value(e, ctx);
    if (val.empty()) return false;
    return exists_check(val);
}

inline void load_xyn_to_cli(const std::string& vfs_path,
                            const std::vector<std::string>& script_args,
                            const std::function<void(const std::string&)>& cli_executor) {
    if (!VFS::getInstance().get_node(vfs_path)) {
        std::cout << "错误: 找不到脚本 " << vfs_path << "\n";
        return;
    }

    std::string content = VFS::getInstance().read_file(vfs_path);
    if (content.empty()) return;

    XynContext ctx;
    std::stringstream ss(content);
    std::string raw_line;

    size_t global_arg_idx = 0;

    while (std::getline(ss, raw_line)) {
        if (!raw_line.empty() && raw_line.back() == '\r') raw_line.pop_back();

        int indent = 0;
        size_t i = 0;
        while (i < raw_line.size() && (raw_line[i] == ' ' || raw_line[i] == '\t')) {
            indent += (raw_line[i] == '\t') ? 4 : 1;
            i++;
        }
        std::string line = raw_line.substr(i);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();

        if (line.empty() || line[0] == '#' || line.rfind("//", 0) == 0) continue;

        {
            size_t pos = 0;
            while ((pos = line.find("$!", pos)) != std::string::npos) {
                std::string rep = (global_arg_idx < script_args.size()) ? script_args[global_arg_idx] : "";
                line.replace(pos, 2, rep);
                pos += rep.length();
                global_arg_idx++;
            }
        }

        bool is_else = (line == "else" || line == "else:");
        bool is_elif = (line.rfind("elif ", 0) == 0 ||
                        line.rfind("elif(", 0) == 0 ||
                        line.rfind("elif (", 0) == 0);

        if (is_else || is_elif) {
            if (ctx.if_stack.empty()) continue;
            auto& fr = ctx.if_stack.back();
            if (fr.indent != indent) continue;

            if (is_else) {
                fr.branch_taken = !fr.any_taken;
                fr.any_taken = true;
                fr.in_else = true;
            } else {
                if (fr.any_taken || fr.in_else) {
                    fr.branch_taken = false;
                } else {
                    bool result = xyn_eval_cond(line.substr(4), ctx, [](const std::string& p) {
                        std::string resolved = resolve_vfs_path_prefix(p);
                        return VFS::getInstance().get_node(resolved) != nullptr;
                    });
                    fr.branch_taken = result;
                    if (result) fr.any_taken = true;
                }
            }
            continue;
        }

        while (!ctx.if_stack.empty() && indent <= ctx.if_stack.back().indent) {
            ctx.if_stack.pop_back();
        }

        if (line.rfind("if ", 0) == 0 || line.rfind("if(", 0) == 0 || line.rfind("if (", 0) == 0) {
            std::string cond_expr = line.substr(2);
            bool parent_ok = ctx.should_exec();
            bool result = false;
            if (parent_ok) {
                result = xyn_eval_cond(cond_expr, ctx, [](const std::string& p) {
                    std::string resolved = resolve_vfs_path_prefix(p);
                    return VFS::getInstance().get_node(resolved) != nullptr;
                });
            }
            XynContext::IfFrame fr;
            fr.indent = indent;
            fr.branch_taken = result;
            fr.any_taken = result;
            fr.in_else = false;
            ctx.if_stack.push_back(fr);
            continue;
        }

        if (!ctx.should_exec()) continue;

        line = xyn_subst_vars(line, ctx);

        {
            size_t pos = line.find("+=");
            if (pos != std::string::npos && pos > 0) {
                std::string name = line.substr(0, pos);
                std::string val  = line.substr(pos + 2);
                auto trim2 = [](std::string& s) {
                    size_t f2 = s.find_first_not_of(" \t");
                    if (f2 == std::string::npos) { s.clear(); return; }
                    size_t l2 = s.find_last_not_of(" \t");
                    s = s.substr(f2, l2 - f2 + 1);
                };
                trim2(name); trim2(val);
                ctx.vars[name] += xyn_eval_value(val, ctx);
                continue;
            }
        }

        {
            size_t pos = line.find('=');
            if (pos != std::string::npos && pos > 0
                && (pos + 1 >= line.size() || line[pos + 1] != '=')
                && line[pos - 1] != '=') {
                std::string name = line.substr(0, pos);
                std::string val  = line.substr(pos + 1);
                auto trim2 = [](std::string& s) {
                    size_t f2 = s.find_first_not_of(" \t");
                    if (f2 == std::string::npos) { s.clear(); return; }
                    size_t l2 = s.find_last_not_of(" \t");
                    s = s.substr(f2, l2 - f2 + 1);
                };
                trim2(name); trim2(val);
                if (!name.empty()) {
                    ctx.vars[name] = xyn_eval_value(val, ctx);
                    continue;
                }
            }
        }

        if (line.rfind("io ", 0) == 0 || line == "io") {
            std::string text = (line.size() > 3) ? line.substr(3) : "";
            std::cout << text << "\n";
            continue;
        }

        if (line.rfind("ipt ", 0) == 0) {
            std::string rest = line.substr(4);
            size_t f = rest.find_first_not_of(" \t");
            if (f == std::string::npos) continue;
            size_t l = rest.find_last_not_of(" \t");
            rest = rest.substr(f, l - f + 1);

            std::string prompt = "";
            std::string name = "";

            size_t first_q = rest.find('"');
            size_t last_q = rest.rfind('"');
            if (first_q != std::string::npos && last_q != std::string::npos && first_q < last_q) {
                prompt = rest.substr(first_q + 1, last_q - first_q - 1);
                name = rest.substr(last_q + 1);
                size_t nf = name.find_first_not_of(" \t");
                if (nf != std::string::npos) name = name.substr(nf);
                else name = "";
            } else {
                name = rest;
            }
            
            if (name.empty()) continue;

            std::string input;
            if (g_backend_client_fd != -1) {
                input = request_frontend_input(prompt);
                if (input == "None") input = ""; 
            } else {
                if (!prompt.empty()) std::cout << prompt << std::flush;
                std::getline(std::cin, input);
            }
            
            ctx.vars[name] = input;
            continue;
        }

        cli_executor(line);
    }
}