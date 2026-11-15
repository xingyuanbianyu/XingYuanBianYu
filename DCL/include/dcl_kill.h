#pragma once
#include <csignal>
#include <map>
#include <string>
#include <vector>
#include <unistd.h>
#include <mutex>
#include <functional>
#include "../../DD/include/mem_driver.h"

struct DCLState;

// ============================================================
// 🚀 大一统：内核进程控制块（合并了原本的 RunProcessInfo）
// ============================================================
struct KernelPCB {
    int v_pid = -1;
    pid_t host_pid = -1;       // 真实的宿主机 PID（如果是内部线程则为 -1）
    
    std::string name;          // 进程名（用于 find_target_key 查找）
    std::string cmd_name;      // 执行的具体命令
    bool is_running = false;
    bool is_background = false;
    bool is_thread = false;
    
    VirtualMemoryBlock* vmem = nullptr;
    bool pending_kill = false;
    int signal_type = SIGTERM;
    
    std::vector<std::string> args;
    std::string log_buffer;
    
    // 🚀 原本 RunProcessInfo 里的字段，全盘接收
    std::string host_path;
    std::string vfs_path;
    std::string log_file;
    std::string start_time;
    
    // 🚀 核心杀招 1：停止回调。内核杀它时，自动调用此函数进行优雅释放（如关闭 Web 服务器端口）
    std::function<void()> stop_callback;

    // 🚀 核心杀招 2：重生指令。内核 ioq 重启它时，直接拿出这个字符串扔给终端重放！
    std::string restart_cmd;
};

extern std::map<int, KernelPCB> process_table;
extern std::mutex process_table_mutex;
extern int next_v_pid;
extern int current_v_pid;

// 信号与杀进程接口
void kernel_send_signal(int v_pid, int sig, const std::string& reason);
void kernel_send_signal(const std::string& target_key, int sig, const std::string& reason);
void kernel_process_signals();

std::string interrupt_process(int v_pid, int sig);
std::string interrupt_process(const std::string& target_key, int sig);

std::string spawn_virtual_process(const std::vector<std::string>& args, 
                                  const std::string& vfs_path, 
                                  bool is_daemon = false, 
                                  bool is_thread = false,
                                  int force_v_pid = -1);

std::string kill_cmd(DCLState& state, const std::vector<std::string>& args);