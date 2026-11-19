#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <set>
#include <functional>
#include <cctype>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdlib>
#include <limits.h>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <arpa/inet.h>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>

#define RESET    "\033[0m"
#define PURPLE   "\033[1;35m"

namespace fs = std::filesystem;

// =================================================================
// 【核心保留】XYBY 语言内核组件
// =================================================================
#include "XYBY/include/char_id_map.h"
#include "DCL/include/dcl_open.h"
#include "DCL/include/dcl_sft.h"
#include "DCL/include/dcl_del.h"
#include "APP/include/dcl_run.h"
#include "DCL/include/dcl_ioq.h"
#include "DCL/include/dcl_kill.h"
#include "APP/include/dcl_nano.h"
#include "DCL/lib/dcl_keyboard.h"
#include "DCL/lib/dcl_cmd_registry.h"
#include "DCL/include/dcl_input.h"
#include "AMM/include/amm_memory.h"
#include "VFS/include/vfs.h"
#include "UI/include/dcl_ui.h"
#include "APP/include/dcl_sound.h"
#include "DD/include/sound.h"
#include "APP/include/dcl_player.h"
#include "DD/include/player.h"
#include "DD/include/config.h"

#include "DD/include/disk_image_driver.h"
#include "DD/include/disk_fs.h"
#include "Kernel/Scheduler.h"
// 🚀 引入 HTTP 服务器头文件
#include "APP/include/http_server.h" 

#include "lang_zh_map.h"
#include "lang_en_map.h"
#include "lib/XYFILE.inc"
#include "lib/VM.inc"
#include "lib/xy_asm.inc" // 🚀 引入内置汇编器

// =================================================================
// 🚀 声明外部的全局回调与进程表
// =================================================================
extern std::function<std::string(const std::string&)> g_input_pid_query_cb;
extern std::function<std::string(const std::string&)> g_input_exec_vfs_cb;
extern void auto_clean_dead_processes();

extern std::vector<uint8_t> compileFromASTString(const std::string& ast);
extern std::vector<uint8_t> compileFromASTStringReg(const std::string& ast);
extern std::vector<uint8_t> compileFromASTStringHeap(const std::string& ast);

#include "XYBY/parser.cpp"
#include "XYBY/debugger.cpp"
#include "lib/utils.inc"
#include "XYBY/executor.cpp"
#include "XYBY/include/config_manager.h"
#include "XYBY/include/exec_priority.h"
#include "DCL/include/dcl_drt.h"
#include "lib/parser_expr.inc"
#include "lib/parser_cond.inc"

std::string g_sudo_pass = "";

// =================================================================
// 🚀 前向声明
// =================================================================
std::string expand_includes(const std::string& code,
                            std::set<std::string>& included_files,
                            const std::string& base_dir = "");

void drt_auto_remount();
void user_init();
void sound_bootstrap();
void player_bootstrap();

#include "lib/parser_ast.inc"

int execute_xy_code(const std::string& code);
int decode_and_run_memory(const std::string& raw_content, const std::string& name = "", int io_fd = -1);

int run_cli_mode(DCLState& state);

#include "lib/runner.inc"
#include "lib/cli.inc"

#include "DD/dd/LoopbackNetDevice.inc"
#include "APP/include/dcl_net.h"

std::unique_ptr<INetworkDevice> g_network_device = std::make_unique<LoopbackNetDevice>();

std::string g_user_cwd;
std::string g_script_dir;
std::string g_exe_root;

#include "lib/main_helpers.inc"

// =================================================================
// 🚀 自动清理临时目录内容
// =================================================================
void auto_clean_vfs_temp_dirs() {
    std::vector<std::string> dirs_to_clean = {"/tmp", "/temp", "/test"};

    std::function<void(const std::string&)> clean_dir = [&](const std::string& dir_path) {
        if (!VFS::getInstance().get_node(dir_path)) return;
        std::vector<std::string> items = VFS::getInstance().list_directory(dir_path);
        for (auto& raw : items) {
            std::string name = strip_ansi(raw);
            name = strip_list_prefix(name);
            if (name == "." || name == ".." || name.empty()) continue;

            std::string full_path = dir_path;
            if (full_path.back() != '/') full_path += "/";
            full_path += name;

            auto node = VFS::getInstance().get_node(full_path);
            if (node) {
                if (std::dynamic_pointer_cast<Directory>(node)) clean_dir(full_path);
                VFS::getInstance().delete_node(full_path);
            }
        }
    };
    for (const auto& dir : dirs_to_clean) clean_dir(dir);
}

extern std::string g_last_xy_return_value;
extern std::string g_await_target_var;

static bool is_backend_running() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "./.xyby.sock", sizeof(addr.sun_path) - 1);
    bool ok = (connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0);
    close(fd);
    return ok;
}

static bool ensure_backend_running(const char* self_path) {
    if (is_backend_running()) return true;
   // std::cout << "[系统] 未检测到 backend，正在启动...\n";

    char abs_path[PATH_MAX] = {0};
    ssize_t len = readlink("/proc/self/exe", abs_path, sizeof(abs_path) - 1);
    std::string exec_path = (len != -1) ? std::string(abs_path) : std::string(self_path);

    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        execl(exec_path.c_str(), exec_path.c_str(), "daemon", (char*)nullptr);
        _exit(127);
    }

    int status;
    for (int i = 0; i < 50; ++i) {
        usleep(100 * 1000);
        if (is_backend_running()) return true;
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) return false;
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0); 
    return false;
}

int main(int argc, char* argv[]) {
        if (false) { decode_and_run_memory(""); }
        
    std::string disk_path = "disk.img";
    bool disk_exists = std::filesystem::exists(disk_path);
    bool skip_first_arg = false;

    if (!disk_exists) {
        uint32_t disk_size_mb = 1024;
        if (argc >= 2) {
            std::string arg1 = argv[1];
            if (!arg1.empty() && arg1.find_first_not_of("0123456789") == std::string::npos) {
                try {
                    disk_size_mb = std::stoi(arg1);
                    if (disk_size_mb < 1) disk_size_mb = 1024;
                    skip_first_arg = true;
                } catch (...) {}
            }
        }
        std::shared_ptr<DiskImageDriver> temp_driver = std::make_shared<DiskImageDriver>();
        temp_driver->create_image(disk_path, disk_size_mb);
        DiskFileSystem::format(temp_driver.get(), "/");
        temp_driver->close();
    } else {
        if (argc >= 2) {
            std::string arg1 = argv[1];
            if (!arg1.empty() && arg1.find_first_not_of("0123456789") == std::string::npos) {
                skip_first_arg = true;
            }
        }
    }

    if (!init_exe_root()) return 1;

    bool force_cli = false;
    bool force_gui = false;
    bool compile_mode = false;
    bool is_asm_mode = false; 
    std::string compile_target = "";
    std::string backend = "stack";

    std::vector<std::string> pos_args;
    for (int i = 1; i < argc; ++i) {
        if (i == 1 && skip_first_arg) continue;
        std::string arg = argv[i];
        if (arg == "--version" || arg == "-V" || arg == "-v" || arg == "--V" || arg == "--v") {
            std::cout << "XingYuanBianYu V1.0" << std::endl;
            return 0;
        }
        if (arg == "-C" || arg == "--cli" || arg == "cli") force_cli = true;
        else if (arg == "-G" || arg == "--gui") force_gui = true;
        else if (arg == "-c") compile_mode = true; 
        else if (arg == "-b" || arg == "-h") {     
            compile_mode = true;
            is_asm_mode = true;
            if (arg == "-h") backend = "heap";     
        }
        else if (arg == "-o" && i + 1 < argc) compile_target = argv[++i];
        else if (arg == "--m") backend = "reg";
        else if (arg == "--h") backend = "heap";
        else if (arg == "--v") backend = "stack";
        else if (!arg.empty() && arg[0] == '-') std::cerr << "未知参数: " << arg << "\n";
        else pos_args.push_back(arg);
    }

    if (argc >= 2 && std::string(argv[1]) == "daemon") {
        try {
            AMM::getInstance().init(256);
            DCLKeyboard::set_context(KeyContext::MAIN);
            CommandRegistry::getInstance().init();
            
            VFS::getInstance().init("/");
            
            std::vector<std::string> sys_dirs = {
                "/home", "/bin", "/lib", "/xyby", "/usr", "/dev", 
                "/data", "/config", "/iso", "/test", "/text", "/input", 
                "/ast", "/ppt", "/ls", "/vm", "/tmp", "/temp",
                "/bin/usr", "/bin/lib", "/lib/lib", 
                "/xyby/data", "/xyby/config", "/xyby/repo"
            };

            for (const auto& dir : sys_dirs) {
                if (!VFS::getInstance().get_node(dir)) VFS::getInstance().create_directory(dir);
            }
            
            SoundCard::getInstance().init();
            XybyConfig::getInstance().init("/xyby/config/config.xyt");
            sound_bootstrap();
            player_bootstrap(); 
            refresh_lib_index();
            drt_auto_remount();
            user_init();

            DCLState state;
            state.cwd = VFS::getInstance().get_cwd();
            VFS::getInstance().set_user_id(0);

            auto_clean_vfs_temp_dirs();

            // =========================================================
            // 🚀 核心改造：注册 input 模块的进程查询回调
            // =========================================================
            g_input_pid_query_cb = [](const std::string& pattern) -> std::string {
                auto_clean_dead_processes();

                std::string result = "--- 进程列表 (匹配: " + pattern + ") ---\n";
                bool found = false;
                
                auto wildcard_match = [](const std::string& str, const std::string& pat) {
                    if (pat == "*" || pat.empty()) return true;
                    size_t s = 0, p = 0, match = 0, starIdx = std::string::npos;
                    while (s < str.size()) {
                        if (p < pat.size() && (pat[p] == '?' || pat[p] == str[s])) { s++; p++; }
                        else if (p < pat.size() && pat[p] == '*') { starIdx = p; match = s; p++; }
                        else if (starIdx != std::string::npos) { p = starIdx + 1; match++; s = match; }
                        else return false;
                    }
                    while (p < pat.size() && pat[p] == '*') p++;
                    return p == pat.size();
                };

                std::string cwd = VFS::getInstance().get_cwd();
                std::lock_guard<std::mutex> lock(process_table_mutex);
                
                for (auto& [v_pid, pcb] : process_table) {
                    if (!pcb.is_running) continue;

                    std::string name = pcb.cmd_name;
                    std::string hpath = pcb.host_path; 
                    std::string vpath = pcb.vfs_path;  
                    std::string host_pid_str = std::to_string(pcb.host_pid);
                    std::string v_pid_str = std::to_string(pcb.v_pid);
                    std::string key_name = pcb.name.empty() ? v_pid_str : pcb.name;
                    
                    bool is_match = false;
                    
                    if (pattern == "*/") {
                        if (hpath.find('/') != std::string::npos || vpath.find('/') != std::string::npos) {
                            is_match = true;
                        }
                    } else if (pattern == "./" || pattern == ".") {
                        if (hpath.rfind("./", 0) == 0 || vpath.rfind("./", 0) == 0 ||
                            hpath.rfind(cwd, 0) == 0 || vpath.rfind(cwd, 0) == 0) {
                            is_match = true;
                        }
                    } else {
                        if (wildcard_match(name, pattern) || 
                            wildcard_match(hpath, pattern) || 
                            wildcard_match(vpath, pattern) || 
                            wildcard_match(host_pid_str, pattern) || 
                            wildcard_match(v_pid_str, pattern) || 
                            wildcard_match(key_name, pattern)) {
                            is_match = true;
                        }
                    }

                    if (is_match) {
                        result += "- [" + key_name + "] " + name + " (VPID: " + v_pid_str + ")\n";
                        if (!vpath.empty()) result += "  虚拟系统入口: " + vpath + "\n";
                        if (!hpath.empty()) result += "  外部系统路径: " + hpath + "\n";
                        result += "  HostPID: " + host_pid_str + "\n";
                        found = true;
                    }
                }
                if (!found) return "未找到匹配 '" + pattern + "' 的进程。";
                return result;
            };

            g_input_exec_vfs_cb = [](const std::string& vfs_path) -> std::string {
                DCLState temp_state; 
                temp_state.cwd = VFS::getInstance().get_cwd();
                int dummy_priv = 1;
                cli_execute_single("exec " + vfs_path, temp_state, dummy_priv);
                return "执行指令已触发。";
            };

            // 🚀 注册 HTTP 动态 API 脚本执行器
            g_http_script_runner = [](const std::string& script_content, const std::string& vfs_path) -> std::string {
                // 使用星缘 OS 的脚本引擎执行 API 脚本
                g_last_xy_return_value = "";
                execute_xy_code(script_content);
                
                // 如果脚本里写了 return "xxx"，这里就能拿到结果
                if (!g_last_xy_return_value.empty()) {
                    return g_last_xy_return_value;
                }
                
                // 如果脚本没有明确 return，给一个默认的 JSON 响应
                return "{\"status\":\"ok\",\"message\":\"Script executed successfully\"}";
            };

            run_backend_server(state);
        } catch (const std::exception& e) {
            std::cerr << "\n[Daemon 致命异常] " << e.what() << "\n";
            return 1;
        } catch (...) {
            std::cerr << "\n[Daemon 未知异常] 发生未知底层错误！\n";
            return 1;
        }
        return 0;
    }

    if (force_cli) {
        if (!ensure_backend_running(argv[0])) return 1;
        signal(SIGINT, SIG_IGN);
        return run_cli_client_mode(pos_args);
    }

    if (pos_args.empty() && !compile_mode) {
        if (!ensure_backend_running(argv[0])) return 1;
        signal(SIGINT, SIG_IGN);
        return run_cli_client_mode(pos_args);
    }

    AMM::getInstance().init(256);
    DCLKeyboard::set_context(KeyContext::MAIN);
    CommandRegistry::getInstance().init();
    signal(SIGINT, SIG_IGN);

    bool need_format = false;
    if (!disk_exists) {
        need_format = true;
    } else {
        int pipefd[2];
        if (pipe(pipefd) == -1) {
            need_format = true;
        } else {
            pid_t pid = fork();
            if (pid == 0) {
                close(pipefd[0]);
                std::string status = "OK";
                try { VFS::getInstance().init("/"); } catch (...) { status = "EXCEPTION"; }
                write(pipefd[1], status.c_str(), status.size());
                close(pipefd[1]);
                _exit(0);
            }
            close(pipefd[1]);
            char buf[64] = {0};
            read(pipefd[0], buf, sizeof(buf) - 1);
            close(pipefd[0]);
            int status = 0;
            waitpid(pid, &status, 0);
            if (WIFSIGNALED(status) || std::string(buf) != "OK") need_format = true;
        }
    }

    if (need_format) {
        std::cerr << "[系统急救] 正在格式化主盘...\n";
        std::shared_ptr<DiskImageDriver> temp_driver = std::make_shared<DiskImageDriver>();
        temp_driver->create_image(disk_path, 1024);
        DiskFileSystem::format(temp_driver.get(), "/");
        temp_driver->close();
    }

    VFS::getInstance().init("/");
    SoundCard::getInstance().init();
    XybyConfig::getInstance().init("/xyby/config/config.xyt");
    sound_bootstrap();
    player_bootstrap();
    refresh_lib_index();

    if (argc >= 2 && !skip_first_arg) {
        std::string first_arg = argv[1];
        if (first_arg == "help" || first_arg == "--help") {
            print_help_index();
            return 0;
        }
    }

    std::string run_file;
    std::vector<std::string> script_argv;
    std::string vfs_run_file;

    if (!pos_args.empty()) {
        const std::string& first = pos_args[0];
        std::string resolved_input = resolve_user_path(first);
        std::error_code ec;

        if (has_allowed_ext(first)) {
            run_file = resolved_input;
            for (size_t k = 1; k < pos_args.size(); ++k) script_argv.push_back(pos_args[k]);
        } else if (fs::is_directory(resolved_input, ec)) {
            fs::path entry = fs::path(resolved_input) / "main.xy";
            if (fs::exists(entry, ec)) {
                run_file = entry.string();
                for (size_t k = 1; k < pos_args.size(); ++k) script_argv.push_back(pos_args[k]);
            } else {
                std::cerr << "[-] 目录下没有 main.xy: " << resolved_input << "\n";
                return 1;
            }
        } else if (fs::is_regular_file(resolved_input, ec)) {
            run_file = resolved_input;
            for (size_t k = 1; k < pos_args.size(); ++k) script_argv.push_back(pos_args[k]);
        } else {
            std::string lib_path;
            if (resolve_lib_command(first, lib_path)) {
                vfs_run_file = lib_path;
                for (size_t k = 1; k < pos_args.size(); ++k) script_argv.push_back(pos_args[k]);
            } else {
                std::string inline_code;
                for (const auto& a : pos_args) {
                    if (!inline_code.empty()) inline_code += " ";
                    inline_code += a;
                }
                script_argv.clear();
                script_argv.push_back(inline_code);
            }
        }
    }

    if (compile_mode) {
        if (run_file.empty() || compile_target.empty()) {
            std::cerr << "[-] 用法: xingyuan -c/-b <源码> -o <目标文件> [--v|--h|--m]\n";
            return 1;
        }

        std::error_code _ec;
        fs::path _rp = fs::weakly_canonical(run_file, _ec);
        if (!_ec) g_script_dir = _rp.parent_path().string();

        if (is_asm_mode) {
            std::string mode_char = "m"; 
            if (backend == "stack") mode_char = "v";
            else if (backend == "heap") mode_char = "h";
            
            try {
                Assembler as(mode_char);
                as.parseFile(run_file);
                as.generate();
                
                std::vector<uint8_t> compiledBytes = as.getOutput();
                
                std::string magic = "XYBM";
                if (mode_char == "v") magic = "XYBV";
                else if (mode_char == "h") magic = "XYBH";
                
                MemoryHeader header(magic);
                std::ofstream out(compile_target, std::ios::binary);
                if (!out) { std::cerr << "[-] 无法创建输出文件\n"; return 1; }
                
                uint32_t valid_bits = (uint32_t)compiledBytes.size() * 8;
                header.writeTo(out, valid_bits);
                out.write(reinterpret_cast<const char*>(compiledBytes.data()), compiledBytes.size());
                out.close();
                
                std::cout << "[汇编器] 成功生成: " << compile_target << " (后端: " << mode_char << ")\n";
                return 0;
            } catch (const std::exception& e) {
                std::cerr << "[-] 汇编失败: " << e.what() << "\n";
                return 1;
            }
        }

        std::ifstream inFile(run_file);
        if (!inFile) { std::cerr << "[-] 无法打开源码文件\n"; return 1; }
        std::stringstream buf;
        buf << inFile.rdbuf();
        std::string source = buf.str();

        std::map<std::string, std::vector<std::string>> modules;
        std::set<std::string> loading_modules;
        std::vector<std::string> ast_lines = parse_source_to_ast(source, modules, loading_modules);

        std::string ast_string;
        for (const auto& line : ast_lines) ast_string += line + "\n";

        std::string magic;
        if (backend == "reg") magic = "XYBM";
        else if (backend == "heap") magic = "XYBH";
        else magic = "XYBV";
        
        MemoryHeader header(magic);
        std::vector<uint8_t> compiledBytes;
        if (backend == "reg") compiledBytes = compileFromASTStringReg(ast_string);
        else if (backend == "heap") compiledBytes = compileFromASTStringHeap(ast_string);
        else compiledBytes = compileFromASTString(ast_string);

        std::ofstream out(compile_target, std::ios::binary);
        if (!out) { std::cerr << "[-] 无法创建输出文件\n"; return 1; }
        
        uint32_t valid_bits = (uint32_t)compiledBytes.size() * 8;
        header.writeTo(out, valid_bits);
        out.write(reinterpret_cast<const char*>(compiledBytes.data()), compiledBytes.size());
        out.close();
        return 0;
    }

    drt_auto_remount();
    user_init();

    Kernel::Scheduler::get().set_executor([](const std::string& fn, const std::vector<std::string>& args) -> std::string {
        std::string target = g_await_target_var.empty() ? "__xy_ret__" : g_await_target_var;
        std::string code = target + " = " + fn + "(";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) code += ",";
            code += args[i];
        }
        code += ")";
        g_last_xy_return_value = "";
        execute_xy_code(code);
        return g_last_xy_return_value;
    });

    if (g_network_device) g_network_device->init();

    DCLState state;
    state.cwd = VFS::getInstance().get_cwd();
    VFS::getInstance().set_user_id(0);

    auto_clean_vfs_temp_dirs();

    if (!vfs_run_file.empty()) {
        if (!VFS::getInstance().get_node(vfs_run_file)) return 1;
        std::string code = VFS::getInstance().read_file(vfs_run_file);
        if (trim(code).empty()) return 1;

        std::string header;
        for (size_t k = 0; k < script_argv.size(); ++k) {
            header += "arg" + std::to_string(k + 1) + " = \"";
            for (char c : script_argv[k]) {
                if (c == '"' || c == '\\' || c == '\n' || c == '\r') header += '\\';
                header += c;
            }
            header += "\"\n";
        }
        std::set<std::string> included;
        std::string full_code = expand_includes(header + code, included);
        return (execute_xy_code(full_code) == 0) ? 0 : 1;
    }

    if (!run_file.empty()) {
        std::error_code _ec;
        fs::path _rp = fs::weakly_canonical(run_file, _ec);
        if (!_ec) g_script_dir = _rp.parent_path().string();

        std::string ext = fs::path(run_file).extension().string();
        if (ext == ".xya" || ext == ".xyb" || ext == ".xye" || ext == ".xyh") {
            std::ifstream ifs(run_file, std::ios::binary);
            if (!ifs.is_open()) return 1;
            std::stringstream buf;
            buf << ifs.rdbuf();
            return decode_and_run_memory(buf.str(), run_file, -1);
        }

        std::ifstream ifs(run_file);
        if (!ifs.is_open()) return 1;
        std::stringstream buf;
        buf << ifs.rdbuf();
        std::string code = buf.str();
        if (trim(code).empty()) return 1;

        std::string header;
        for (size_t k = 0; k < script_argv.size(); ++k) {
            header += "arg" + std::to_string(k + 1) + " = \"";
            for (char c : script_argv[k]) {
                if (c == '"' || c == '\\' || c == '\n' || c == '\r') header += '\\';
                header += c;
            }
            header += "\"\n";
        }
        std::set<std::string> included;
        std::string full_code = expand_includes(header + code, included);
        return (execute_xy_code(full_code) == 0) ? 0 : 1;
    }

    if (!script_argv.empty()) {
        std::set<std::string> included;
        std::string full = expand_includes(script_argv[0], included);
        return (execute_xy_code(full) == 0) ? 0 : 1;
    }

    return 0;
}