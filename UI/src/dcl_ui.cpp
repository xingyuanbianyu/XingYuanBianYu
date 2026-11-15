#include "../include/dcl_ui.h"
#include "../../DCL/lib/dcl_keyboard.h"
#include "../../DCL/include/dcl_kill.h"   // 【核心新增】引入 process_table, KernelPCB, kernel_process_signals
#include "../../AMM/include/amm_memory.h"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>

// =========================================================================
// 【前提假设】：
// dcl_kill.h 中已经声明了：
// struct KernelPCB { ... };
// extern std::map<int, KernelPCB> process_table;
// extern void kernel_process_signals();
// amm_memory.h 中已经补充了 get_quota() 和 get_used() 的 public 接口
// =========================================================================

// 获取终端大小
static void get_window_size(int& rows, int& cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        rows = 24; cols = 80;
    } else {
        rows = ws.ws_row;
        cols = ws.ws_col;
    }
}

// 绘制居中面板
static void draw_ui(DCLState& state, int rows, int cols) {
    // 计算面板尺寸（占屏幕 60% ~ 70%）
    int ui_width = std::max(40, std::min(80, cols * 6 / 10));
    int ui_height = std::max(10, std::min(20, rows * 7 / 10));
    
    int start_row = (rows - ui_height) / 2;
    int start_col = (cols - ui_width) / 2;

    // 全屏清空 + 居中定位
    std::cout << "\033[2J\033[H";

    // 构建顶部边框
    std::string top_border = "┌";
    for (int i = 0; i < ui_width - 2; ++i) top_border += "─";
    top_border += "┐";

    // 构建底部边框
    std::string bottom_border = "└";
    for (int i = 0; i < ui_width - 2; ++i) bottom_border += "─";
    bottom_border += "┘";

    // 打印顶部边框
    std::cout << "\033[" << start_row << ";" << start_col << "H" << top_border;

    // 准备内容
    std::vector<std::string> lines;
    lines.push_back(" 星缘编语 控制台 (UI 模式)");
    lines.push_back("");
    // 【核心修改】state.cwd 已经是 std::string，直接拼接
    lines.push_back(" 当前工作目录: " + state.cwd); 
    
    // 【内核化改造】从 AMM 获取内核内存状态
    lines.push_back(" 内存配额: " + std::to_string(AMM::getInstance().get_quota() / 1024 / 1024) + " MB");
    lines.push_back(" 已用内存: " + std::to_string(AMM::getInstance().get_used() / 1024 / 1024) + " MB");
    
    // 【核心修改】移除 background_processes，改用内核进程表 process_table
    lines.push_back(" 内核进程数: " + std::to_string(process_table.size()));
    
    lines.push_back("");
    lines.push_back(" 操作提示:");
    lines.push_back(" [ Q ] 退出 UI，返回命令行");
    lines.push_back(" [ K ] 强制退出内核");

    // 逐行打印内容（带左右边框）
    for (int i = 0; i < ui_height - 2; ++i) {
        std::cout << "\033[" << (start_row + i + 1) << ";" << start_col << "H│";
        
        if (i < (int)lines.size()) {
            std::string content = lines[i];
            if ((int)content.length() > ui_width - 4) {
                content = content.substr(0, ui_width - 4);
            }
            std::cout << " " << content;
            int padding = ui_width - 3 - content.length();
            for (int j = 0; j < padding; ++j) std::cout << " ";
        } else {
            for (int j = 0; j < ui_width - 2; ++j) std::cout << " ";
        }
        std::cout << "│";
    }

    // 打印底部边框
    std::cout << "\033[" << (start_row + ui_height - 1) << ";" << start_col << "H" << bottom_border;
    std::cout.flush();
}

// UI 主循环
bool start_ui(DCLState& state) {
    int rows, cols;
    get_window_size(rows, cols);
    draw_ui(state, rows, cols);

    // 设置虚拟键盘为 NANO 上下文（当作普通输入，不需要历史记录逻辑）
    DCLKeyboard::set_context(KeyContext::NANO);

    bool ui_running = true;
    while (ui_running) {
        // ================= 【核心新增】 =================
        // 在主循环中周期处理内核信号，防止 UI 阻塞时 AMM 信号卡死
        kernel_process_signals();

        // 监听按键
        KeyInput key = DCLKeyboard::read();

        if (key.event == KeyEvent::TEXT || key.event == KeyEvent::COMMAND) {
            std::string cmd = key.raw;
            
            // 转换为小写判断
            std::string lower_cmd = cmd;
            std::transform(lower_cmd.begin(), lower_cmd.end(), lower_cmd.begin(), ::tolower);

            if (lower_cmd == "q" || lower_cmd == "quit") {
                ui_running = false; // 退出 UI，返回 CLI
            } else if (lower_cmd == "k" || lower_cmd == "kill") {
                // 如果在 UI 里输入 kill，直接退出整个主程序（内核断电）
                DCLKeyboard::set_context(KeyContext::MAIN);
                std::cout << "\n[*] 收到强制退出指令，正在执行内核停机流程...\n";
                return false; 
            }
        }
    }

    // 恢复终端上下文
    DCLKeyboard::set_context(KeyContext::MAIN);
    std::cout << "\033[2J\033[H" << std::flush; // 退出时清屏
    return true;
}