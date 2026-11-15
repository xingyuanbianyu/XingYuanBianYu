#pragma once
#include <string>
#include <functional>

void start_http_server(int port, const std::string& root_path);
void stop_http_server();

// 🚀 注册执行脚本的回调
extern std::function<std::string(const std::string& script_content, const std::string& vfs_path)> g_http_script_runner;