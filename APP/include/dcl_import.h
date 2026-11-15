#ifndef DCL_IMPORT_H
#define DCL_IMPORT_H

#include "../../DCL/include/dcl_open.h"
#include <string>
#include <vector>

// 🚀 宿主机文件/文件夹导入指令
std::string import_cmd(DCLState& state, const std::vector<std::string>& args);

#endif