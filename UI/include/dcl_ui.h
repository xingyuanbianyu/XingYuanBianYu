#pragma once
#include "../../DCL/include/dcl_open.h"
#include "../../AMM/include/amm_memory.h"
#include "dcl_run.h"

// 启动控制台 UI
// 返回: true 表示正常退出UI，false 表示要求主进程退出
bool start_ui(DCLState& state);