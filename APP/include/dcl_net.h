#pragma once
#include <string>
#include <vector>

std::string net_cmd(const std::vector<std::string>& args);

// 🚀 新增：供 dcl_kill.cpp 等系统内核调用的 Web 服务器强制停止接口
bool net_stop_web_server_by_vpid(int v_pid);