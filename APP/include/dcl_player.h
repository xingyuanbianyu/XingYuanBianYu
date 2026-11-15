#pragma once
#include <string>
#include <vector>

// 🚀 播放器开机初始化（在 main.cpp 中调用）
void player_bootstrap();

std::string play_cmd(const std::vector<std::string>& args);