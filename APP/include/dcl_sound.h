#ifndef DCL_SOUND_H
#define DCL_SOUND_H

#include <string>
#include <vector>

// 初始化声卡
void sound_bootstrap();

// CLI 命令入口
std::string sound_cmd(const std::vector<std::string>& args);

#endif