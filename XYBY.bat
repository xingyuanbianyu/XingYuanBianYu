@echo off
echo [构建] 正在一键编译星缘OS...

g++ -std=c++17 main.cpp DCL/src/*.cpp DCL/lib/*.cpp VFS/src/*.cpp DD/dd/*.cpp AMM/src/*.cpp UI/src/*.cpp XYBY/parser.cpp XYBY/executor.cpp XYBY/debugger.cpp -I include -I DCL/include -I VFS/include -I AMM/include -I UI/include -I DD/include -I DD/dd -I XYBY/include -o Data_Command_Line.exe -static

if %errorlevel% neq 0 (
    echo.
    echo [错误] 编译失败了！请往上翻查看红字报错。
    pause
    exit /b
)

echo.
echo [构建] 编译成功！正在启动星缘OS...
echo ========================================
Data_Command_Line.exe
pause