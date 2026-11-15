#include "../include/dcl_run.h"
#include "../../DCL/include/dcl_kill.h"
#include "dcl_nano.h"
#include "../../DCL/lib/dcl_cmd_registry.h"
#include "../../AMM/include/amm_memory.h"
#include "../../DD/include/mem_driver.h"
#include "../../VFS/include/vfs.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <vector>
#include <string>
#include <map>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include <random>
#include <filesystem>
#include <mutex>
#include <cerrno>
#include <glob.h> 

void cli_execute_single(const std::string& cmd_line, DCLState& state, int& cli_priv);

// ================= 辅助函数 =================

static std::string generate_unique_key() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(1, 999999);
    std::string key;
    std::lock_guard<std::mutex> lock(process_table_mutex);
    do {
        key = std::to_string(dist(gen));
    } while ([&]() {
        for (const auto& p : process_table) {
            if (p.second.name == key) return true;
        }
        return false;
    }());
    return key;
}

// 🚀 自动巡检清理僵尸进程
void auto_clean_dead_processes() {
    std::vector<int> to_clean;
    {
        std::lock_guard<std::mutex> lock(process_table_mutex);
        for (auto& [v_pid, pcb] : process_table) {
            if (pcb.host_pid > 0 && pcb.is_running) {
                bool alive = true;
                if (kill(pcb.host_pid, 0) == -1 && errno == ESRCH) {
                    alive = false;
                } else {
                    int status;
                    pid_t w = waitpid(pcb.host_pid, &status, WNOHANG);
                    if (w == pcb.host_pid || (w == -1 && errno == ECHILD)) {
                        alive = false;
                    }
                }
                if (!alive) to_clean.push_back(v_pid);
            }
        }
    }
    for (int v_pid : to_clean) {
        interrupt_process(v_pid, SIGKILL);
    }
}

// 🚀 查找逻辑
static int find_target_v_pid(const std::string& target) {
    try {
        int v = std::stoi(target);
        std::lock_guard<std::mutex> lock(process_table_mutex);
        if (process_table.find(v) != process_table.end()) return v;
    } catch (...) {}

    std::lock_guard<std::mutex> lock(process_table_mutex);
    for (auto& [v_pid, pcb] : process_table) {
        if (pcb.name == target || 
            pcb.cmd_name == target || 
            std::to_string(pcb.host_pid) == target ||
            pcb.host_path == target ||
            pcb.vfs_path == target) {
            return v_pid;
        }
    }
    return -1;
}

// ================= 核心进程调度 =================
std::string spawn_process(const std::vector<std::string>& args, const std::string& host_exec_path, bool is_daemon, const std::string& force_key) {
    if (args.empty()) return "错误: 缺少参数";

    std::string cmd_name = args[0];
    // 确定唯一的 Key（如果未指定则随机生成）
    std::string unique_key = force_key.empty() ? generate_unique_key() : force_key;
    
    std::string host_log_path = "runtime/log_" + unique_key + ".xyg";
    std::filesystem::create_directories("runtime");

    // 🚀 核心修复：构造精准的 restart_cmd 字符串（强制带上确定的 unique_key）
    std::string cmd_to_replay = "run";
    for (const auto& a : args) {
        cmd_to_replay += " " + a;
    }
    if (is_daemon) cmd_to_replay += " -D";
    
    // 无论 force_key 是否为空，只要确定了 unique_key，就一定要带上 -P
    cmd_to_replay += " -P " + unique_key;

    // 🚀 通配符展开
    std::vector<std::string> expanded_args;
    for (const auto& a : args) {
        if (a.find('*') != std::string::npos || a.find('?') != std::string::npos) {
            glob_t glob_result;
            if (glob(a.c_str(), GLOB_NOCHECK, nullptr, &glob_result) == 0) {
                for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
                    expanded_args.push_back(glob_result.gl_pathv[i]);
                }
                globfree(&glob_result);
            } else {
                expanded_args.push_back(a);
            }
        } else {
            expanded_args.push_back(a);
        }
    }

    std::vector<char*> argv_c;
    for (const auto& a : expanded_args) argv_c.push_back(const_cast<char*>(a.c_str()));
    argv_c.push_back(nullptr);

    pid_t pid = fork();
    if (pid == 0) {
        if (is_daemon) {
            setsid();
            freopen("/dev/null", "r", stdin);
            freopen(host_log_path.c_str(), "w", stdout);
            freopen(host_log_path.c_str(), "w", stderr);
        }
        execv(host_exec_path.c_str(), argv_c.data());
        _exit(127);
    } else if (pid > 0) {
        int new_v_pid = -1;
        {
            std::lock_guard<std::mutex> lock(process_table_mutex);
            new_v_pid = next_v_pid++;
            
            KernelPCB pcb;
            pcb.v_pid = new_v_pid;
            pcb.host_pid = pid;
            pcb.name = unique_key;
            pcb.cmd_name = cmd_name;
            pcb.host_path = host_exec_path;
            pcb.vfs_path = args[0]; 
            pcb.args = args;
            pcb.log_file = host_log_path;
            pcb.is_running = true;
            pcb.is_background = is_daemon;
            pcb.is_thread = false;
            pcb.signal_type = SIGTERM;
            
            // 🚀 保存重启指令
            pcb.restart_cmd = cmd_to_replay;
            
            time_t now = time(0);
            char buf[100];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
            pcb.start_time = buf;

            process_table[new_v_pid] = pcb;
        }

        if (!is_daemon) {
            int status = 0;
            waitpid(pid, &status, 0);
            interrupt_process(new_v_pid, SIGKILL); 
        }
        return ""; 
    }
    return "错误: fork 失败";
}

// ================= 运行命令入口 =================
std::string run_cmd(DCLState& state, const std::vector<std::string>& args) {
    if (args.empty()) return "用法: run <命令> [参数...] [-D 后台运行] | run log <ID/名称> | run list";

    auto_clean_dead_processes();

    // --- 列表 ---
    if (args[0] == "list") {
        std::string result = "--- 内核进程任务列表 ---\n";
        std::lock_guard<std::mutex> lock(process_table_mutex);
        if (process_table.empty()) return "[-] 内核中当前没有正在运行的进程。";
        
        for (auto& [v_pid, pcb] : process_table) {
            result += "- [" + pcb.name + "] VPID:" + std::to_string(pcb.v_pid) + " " + pcb.cmd_name + "\n";
            if (!pcb.vfs_path.empty())  result += "  虚拟系统入口: " + pcb.vfs_path + "\n";
            if (!pcb.host_path.empty()) result += "  外部系统路径: " + pcb.host_path + "\n";
            result += "  HostPID: " + std::to_string(pcb.host_pid) + "\n";
            result += "  启动时间: " + pcb.start_time + "\n";
        }
        return result;
    }
    
    // --- 日志 ---
    if (args[0] == "log") {
        if (args.size() < 2) return "用法: run log <ID/名称>";
        std::string target = args[1];
        
        std::string log_path;
        std::string cmd_title;
        {
            std::lock_guard<std::mutex> lock(process_table_mutex);
            int v_pid = find_target_v_pid(target);
            if (v_pid == -1) return "[-] 未找到进程 '" + target + "'。";
            
            log_path = process_table[v_pid].log_file;
            cmd_title = process_table[v_pid].cmd_name;
        }
        
        std::ifstream ifs(log_path);
        if (!ifs.is_open()) return "(暂无日志记录)";
        std::stringstream buf;
        buf << ifs.rdbuf();
        return "--- 日志 (" + cmd_title + ") ---\n" + buf.str();
    }

    bool is_daemon = false;
    std::string force_key = "";
    std::vector<std::string> cmd_args = args;

    auto it_d = std::find(cmd_args.begin(), cmd_args.end(), "-D");
    if (it_d != cmd_args.end()) { is_daemon = true; cmd_args.erase(it_d); }

    auto it_p = std::find(cmd_args.begin(), cmd_args.end(), "-P");
    if (it_p != cmd_args.end() && it_p + 1 != cmd_args.end()) {
        force_key = *(it_p + 1);
        cmd_args.erase(it_p, it_p + 2); 
    }

    if (cmd_args.empty()) return "错误: 缺少要执行的命令。";
    std::string cmd_name = cmd_args[0];

    // --- 处理 kill / ioq ---
    if (cmd_args.size() >= 2 && (cmd_args.back() == "kill" || cmd_args.back() == "ioq")) {
        std::string action = cmd_args.back();
        std::string target = cmd_args[0];
        
        int v_pid = -1;
        KernelPCB target_pcb;
        {
            std::lock_guard<std::mutex> lock(process_table_mutex);
            v_pid = find_target_v_pid(target);
            if (v_pid == -1) return "错误: 未找到目标 '" + target + "'。";
            target_pcb = process_table[v_pid];
        }

        // 🚀 统一走内核的 interrupt_process 销毁
        interrupt_process(v_pid, SIGTERM);

        if (action == "ioq") {
            // 🚀 核心：不再拼接，直接使用原先保存好的字符串重放！
            if (!target_pcb.restart_cmd.empty()) {
                int cli_priv = 1;
                cli_execute_single(target_pcb.restart_cmd, state, cli_priv);
                return "[+] 已重放指令: " + target_pcb.restart_cmd;
            } else {
                return "[-] 找不到重启指令，无法重放。";
            }
        }
        
        return "[+] 已向 '" + target_pcb.name + "' 发送终止信号。";
    }

    // --- 启动新进程 ---
    std::string sys_path = CommandRegistry::getInstance().find(cmd_name);
    if (sys_path.empty()) return "错误: 系统中未找到外部指令 '" + cmd_name + "'。";

    return spawn_process(cmd_args, sys_path, is_daemon, force_key);
}