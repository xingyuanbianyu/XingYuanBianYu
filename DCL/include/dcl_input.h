#pragma once

#include <string>
#include <vector>

// 输入命令入口
//   input 查询 <name>                       → 本地优先，其次服务器
//   input 查询 <name> open <dir>            → 保存到 <dir>
//   input 查询 <name> open <dir> exec       → 保存后执行
//   input install <pkg>                     → 下载安装到 data/packages/
std::string input_cmd(const std::vector<std::string>& args);