#pragma once
#include <string>

std::string http_download(const std::string& url, const std::string& host_dest_path);
std::string http_get_string(const std::string& url); // 🚀 新增：直接拉取字符串