#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include "dcl_open.h"

// 🚀 RunProcessInfo 和 background_processes 已经全部合并到 KernelPCB 的 process_table 中了，此处彻底废弃！

std::string run_cmd(DCLState& state, const std::vector<std::string>& args);

// force_key 依然保留，用于在 process_table 里自定义进程名
std::string spawn_process(const std::vector<std::string>& args, const std::string& host_exec_path, bool is_daemon, const std::string& force_key = "");