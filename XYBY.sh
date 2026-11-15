#!/bin/bash

echo "[构建] 开始编译星缘编语v1.0"

# 1. 自动查找所有 .cpp 文件（排除了包含 main 函数的 executor/parser 等，防止重定义）
SOURCES=$(find . -name "*.cpp" -not -path "./.*" -not -path "./build/*" -not -path "./XYBY/executor.cpp" -not -path "./XYBY/parser.cpp" -not -path "./XYBY/debugger.cpp")

# 2. 包含头文件路径
INCLUDES="-I include -I DCL/include -I VFS/include -I AMM/include -I UI/include -I DD/include -I DD/dd -I XYBY/include -I APP/include -I Kernel"

echo "[编译] 编译XingYuanBianYu-C++版本"
# 🚀 核心修改：在末尾加上 -lssl -lcrypto
g++ -std=c++17 $SOURCES $INCLUDES -o Data_Command_Line.xy -lrt -pthread -ldl -lssl -lcrypto

# 4. 检查编译结果
if [ $? -ne 0 ]; then
    echo "[错误] 编译失败！请检查红字报错。"
    exit 1
fi

# 5. 赋予执行权限
chmod +x Data_Command_Line.xy

# =========================================================
# 6. 启动
# =========================================================
echo "[构建] 编译成功！正在启动星缘OS..."
echo "========================================"

if [ -f "disk.img" ]; then
    ./Data_Command_Line.xy
else
    ./Data_Command_Line.xy 1024
fi