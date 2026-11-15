#include "../include/dcl_kill.h"
#include "../include/dcl_run.h"
#include "../../AMM/include/amm_memory.h"
#include "../../DD/include/mem_driver.h"
#include "../../VFS/include/vfs.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <csignal>   
#include <vector>
#include <map>
#include <queue>
#include <mutex>
#include <thread>    
#include <cerrno>
#include <sys/wait.h> // 🚀 修复 1：必须包含此头文件，才能使用 waitpid

// =========================================================================
// 🚀 大一统：全局唯一进程表
// =========================================================================
std::map<int, KernelPCB> process_table;
std::mutex process_table_mutex;
int next_v_pid = 1;
int current_v_pid = 0; 

// =========================================================================
// 内核级信号队列
// =========================================================================
struct KernelSignal {
    std::string target_key;
    int signal_type;
    std::string reason;
};

static std::queue<KernelSignal> kernel_signal_queue;
static std::mutex signal_queue_mutex;

void kernel_send_signal(const std::string& target_key, int sig, const std::string& reason) {
    std::lock_guard<std::mutex> lock(signal_queue_mutex);
    kernel_signal_queue.push({target_key, sig, reason});
}

void kernel_process_signals() {
    while (true) {
        KernelSignal sig;
        {
            std::lock_guard<std::mutex> lock(signal_queue_mutex);
            if (kernel_signal_queue.empty()) break;
            sig = kernel_signal_queue.front();
            kernel_signal_queue.pop();
        } 

        std::string res = interrupt_process(sig.target_key, sig.signal_type);
        if (!res.empty()) {
            std::cerr << "[-] 内核信号处理失败: " << res << "\n";
        }
    }
}

// =========================================================================
// 🚀 核心：统一查找 v_pid (支持数字、名字、模糊匹配)
// =========================================================================
static int find_v_pid_by_string(const std::string& target) {
    std::lock_guard<std::mutex> lock(process_table_mutex);
    
    // 1. 优先当作纯数字 v_pid 或 host_pid 查找
    try {
        int v = std::stoi(target);
        if (process_table.find(v) != process_table.end()) {
            return v;
        }
        for (auto& [v_pid, pcb] : process_table) {
            if (pcb.host_pid == v) return v_pid;
        }
    } catch (...) {}

    // 2. 字符串全匹配/模糊匹配
    std::string lower_target = target;
    std::transform(lower_target.begin(), lower_target.end(), lower_target.begin(), ::tolower);

    // 第一轮：精确匹配
    for (auto& [v_pid, pcb] : process_table) {
        if (pcb.name == target || 
            pcb.cmd_name == target || 
            pcb.host_path == target || 
            pcb.vfs_path == target) {
            return v_pid;
        }
    }

    // 第二轮：包含匹配 (模糊查杀)
    for (auto& [v_pid, pcb] : process_table) {
        std::string lower_name = pcb.cmd_name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        if (lower_name.find(lower_target) != std::string::npos) {
            return v_pid;
        }
    }

    return -1;
}

// =========================================================================
// 🚀 统一进程销毁逻辑（核心实现，只保留这一个 int 版本）
// =========================================================================
std::string interrupt_process(int v_pid, int sig) {
    std::lock_guard<std::mutex> lock(process_table_mutex);
    auto it = process_table.find(v_pid);
    if (it == process_table.end()) return "[-] 拒绝操作: 该进程不存在或已被回收。";

    KernelPCB& pcb = it->second;

    // 1. 优先执行内部服务回调 (比如关闭 Web 服务器)
    if (pcb.stop_callback) {
        pcb.stop_callback();
    }

    // 2. 清理宿主机真实进程
    if (pcb.host_pid > 0) {
        kill(-pcb.host_pid, sig); 
        kill(pcb.host_pid, sig);  
        waitpid(pcb.host_pid, nullptr, WNOHANG); // 🚀 此处用到了 waitpid
    }

    // 3. 标记状态并清理 AMM 内存
    pcb.is_running = false;
    try { AMM::getInstance().reclaim_by_v_pid(pcb.v_pid); } catch (...) {}
    
    process_table.erase(it);
    return ""; 
}

// 🚀 重载：接收 string 的版本，自动转调 int 版本
std::string interrupt_process(const std::string& target_key, int sig) {
    if (target_key.empty()) return "[-] 错误: 无效的进程 Key";
    
    int v_pid = find_v_pid_by_string(target_key);
    if (v_pid == -1) return "[-] 拒绝操作: 该进程不存在或已被回收。";
    
    return interrupt_process(v_pid, sig);
}

// =========================================================================
// kill 命令入口
// =========================================================================
std::string kill_cmd(DCLState& state, const std::vector<std::string>& args) {
    // 无参数：清理所有进程并退出
    if (args.empty()) {
        {
            std::lock_guard<std::mutex> lock(process_table_mutex);
            for (auto& [v_pid, pcb] : process_table) {
                if (pcb.stop_callback) pcb.stop_callback();
                if (pcb.host_pid > 0) { kill(-pcb.host_pid, SIGKILL); kill(pcb.host_pid, SIGKILL); }
            }
            process_table.clear();
        }
        _exit(0); 
    }

    // kill 0：内核级全盘清空
    if (args[0] == "0") {
        if (VFS::getInstance().get_role() != UserRole::R0_ULTIMATE) 
            return "权限不足: 0 kill 需要 R0 内核权限";
        {
            std::lock_guard<std::mutex> lock(process_table_mutex);
            for (auto& [v_pid, pcb] : process_table) {
                if (pcb.stop_callback) pcb.stop_callback();
                if (pcb.host_pid > 0) { kill(-pcb.host_pid, SIGKILL); kill(pcb.host_pid, SIGKILL); }
            }
            process_table.clear();
        }
        return "__SYS_EXIT__";
    }

    // 关机重启
    if (args[0] == "-q") {
        system("su -c \"reboot -p\"");
        _exit(0);
    }

    // 解析强杀标志
    bool is_force = false;
    std::vector<std::string> remaining_args = args;
    if (!remaining_args.empty() && remaining_args[0] == "-9") {
        is_force = true;
        remaining_args.erase(remaining_args.begin());
    }

    if (remaining_args.empty()) return ""; 

    std::string target = remaining_args[0];
    int sig = is_force ? SIGKILL : SIGTERM;

    // 查找并清理匹配项
    std::vector<int> matched_v_pids;
    {
        std::lock_guard<std::mutex> lock(process_table_mutex);
        std::string lower_target = target;
        std::transform(lower_target.begin(), lower_target.end(), lower_target.begin(), ::tolower);

        for (auto& [v_pid, pcb] : process_table) {
            std::string lower_name = pcb.cmd_name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
            
            if (pcb.name == target || pcb.cmd_name == target || std::to_string(pcb.v_pid) == target) {
                matched_v_pids.push_back(v_pid);
            } else if (lower_name.find(lower_target) != std::string::npos && pcb.is_running) {
                matched_v_pids.push_back(v_pid);
            }
        }
    }

    if (matched_v_pids.empty()) return "错误: 未找到目标进程 '" + target + "'";
    
    for (int v : matched_v_pids) {
        interrupt_process(v, sig);
    }

    return "";
}

// =========================================================================
// 🚀 终极兼容层：任何地方传了 int PID，自动转 string 交给新版逻辑
// =========================================================================
void kernel_send_signal(int v_pid, int sig, const std::string& reason) {
    kernel_send_signal(std::to_string(v_pid), sig, reason);
}

// 🚀 注意：这里不再重复写 interrupt_process(int, int)，因为上面已经有了。