#include "../include/dcl_ioq.h"
#include "../include/dcl_run.h"
#include "../include/dcl_kill.h"
#include "../../AMM/include/amm_memory.h"
#include "../../DD/include/mem_driver.h"
#include "../../VFS/include/vfs.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <vector>
#include <map>
#include <mutex>
#include <signal.h>
#include <fcntl.h>

extern std::string g_exe_root;
void cli_execute_single(const std::string& cmd_line, DCLState& state, int& cli_priv);

std::string ioq_cmd(DCLState& state, const std::vector<std::string>& args) {
    // ================= 无参数：物理重启内核自身 =================
    if (args.empty()) {
        if (VFS::getInstance().get_role() != UserRole::R0_ULTIMATE) {
            return "权限不足: 重启内核需要 R0 权限，请使用 'xyby ioq'";
        }

        {
            std::lock_guard<std::mutex> lock(process_table_mutex);
            for (auto& [v_pid, pcb] : process_table) {
                if (pcb.stop_callback) pcb.stop_callback(); 
                if (pcb.host_pid > 0) {
                    kill(-pcb.host_pid, SIGKILL);
                    kill(pcb.host_pid, SIGKILL);
                }
                try { AMM::getInstance().reclaim_by_v_pid(v_pid); } catch(...) {}
            }
            process_table.clear();
        }

        AMM::getInstance().init(256);

        std::string bin_path = g_exe_root + "/Data_Command_Line.xy";
        std::vector<std::string> orig_args;
        {
            std::ifstream cmdline_file("/proc/self/cmdline");
            if (cmdline_file.is_open()) {
                std::string s;
                while (std::getline(cmdline_file, s, '\0')) {
                    if (!s.empty()) orig_args.push_back(s);
                }
            }
        }

        std::vector<char*> new_argv;
        new_argv.push_back((char*)bin_path.c_str());
        for (size_t i = 1; i < orig_args.size(); ++i) new_argv.push_back((char*)orig_args[i].c_str());
        new_argv.push_back(nullptr);

        std::cout.flush(); std::cerr.flush();

        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            freopen("/dev/null", "r", stdin);
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);
            for (int fd = 3; fd < 1024; ++fd) close(fd);
            execv(bin_path.c_str(), new_argv.data());
            _exit(1);
        } else if (pid > 0) {
            kill(getppid(), SIGKILL);
            unlink("./.xyby.sock");
            _exit(0);
        } else {
            return "错误: fork 失败，内核无法重启，旧进程已保留";
        }
    }

    // ================= 1. 处理 -q 重启系统 =================
    if (args[0] == "-q") {
        if (VFS::getInstance().get_role() != UserRole::R0_ULTIMATE) return "权限不足";
        int ret = system("su -c reboot");
        return ret == 0 ? "" : "[-] 重启失败"; 
    }

    // ================= 2. 解析 -9 强杀标志 =================
    bool is_force = false;
    std::vector<std::string> remaining_args = args;
    if (!remaining_args.empty() && remaining_args[0] == "-9") {
        is_force = true;
        remaining_args.erase(remaining_args.begin());
    }

    if (remaining_args.empty()) return ""; 
    if (static_cast<int>(VFS::getInstance().get_role()) > static_cast<int>(UserRole::R3_SYS_ADMIN)) {
        return "权限不足: 需要 R3 权限";
    }

    std::string target = remaining_args[0];
    int sig = is_force ? SIGKILL : SIGTERM;

    // ================= 3. 在统一内核进程表中查找 =================
    std::vector<int> matched_v_pids;

    {
        std::lock_guard<std::mutex> lock(process_table_mutex);
        
        for (auto& [v_pid, pcb] : process_table) {
            if (!pcb.is_running) continue;
            if (pcb.name == target || 
                pcb.cmd_name == target || 
                pcb.host_path == target || 
                pcb.vfs_path == target || 
                std::to_string(pcb.host_pid) == target || 
                std::to_string(pcb.v_pid) == target) {
                matched_v_pids.push_back(v_pid);
            }
        }
        
        if (matched_v_pids.empty()) {
            std::string lower_target = target;
            std::transform(lower_target.begin(), lower_target.end(), lower_target.begin(), ::tolower);
            for (auto& [v_pid, pcb] : process_table) {
                if (!pcb.is_running) continue;
                std::string lower_name = pcb.cmd_name;
                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                if (lower_name.find(lower_target) != std::string::npos) {
                    matched_v_pids.push_back(v_pid);
                }
            }
        }
    }

    if (matched_v_pids.empty()) return ""; 

    // ================= 4. 执行内核级重启 (纯粹指令重放 + PID劫持) =================
    for (int v_pid : matched_v_pids) {
        std::string cmd_to_replay;
        std::string pcb_name;
        
        // 🚀 拿出重放指令和原始名字
        {
            std::lock_guard<std::mutex> lock(process_table_mutex);
            auto it = process_table.find(v_pid);
            if (it != process_table.end()) {
                cmd_to_replay = it->second.restart_cmd;
                pcb_name = it->second.name;
            }
        }

        // 1. 内核级强杀（自动走 stop_callback 关闭资源）
        std::string kill_res = interrupt_process(v_pid, sig);
        if (!kill_res.empty()) {
            continue; 
        }
        
        usleep(100000); 

        // 2. 内核级拉起（原封不动把字符串扔回去）
        if (!cmd_to_replay.empty()) {
            int cli_priv = 1;
            cli_execute_single(cmd_to_replay, state, cli_priv);

            // 🚀 3. 核心修复：劫持新进程的 ID，换回旧 ID，实现原地复活！
            std::lock_guard<std::mutex> lock(process_table_mutex);
            int new_v_pid = -1;
            
            // 因为我们在 restart_cmd 里强制绑定了原本的名字，所以直接按名字找新起的进程
            for (auto& [n_pid, n_pcb] : process_table) {
                if (n_pid != v_pid && n_pcb.name == pcb_name) {
                    new_v_pid = n_pid;
                    break;
                }
            }
            
            if (new_v_pid != -1) {
                // 把新进程结构体偷出来
                KernelPCB new_pcb = process_table[new_v_pid];
                // 抹去它原来的新身份证
                process_table.erase(new_v_pid);
                // 给它换上旧的身份证
                new_pcb.v_pid = v_pid;
                // 原地塞回表里
                process_table[v_pid] = new_pcb;
            }
        }
    }

    return ""; 
}