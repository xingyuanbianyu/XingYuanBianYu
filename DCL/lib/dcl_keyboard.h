#pragma once
#include <string>
#include <vector>

// ================= 上下文模式 =================
enum class KeyContext {
    MAIN,       // 主循环模式
    NANO,       // 编辑器模式
};

// ================= 按键事件枚举 =================
enum class KeyEvent {
    NONE,           // 空输入
    TEXT,           // 普通文本（回车提交）
    COMMAND,        // 以 : 开头的命令
    HISTORY_UP,     // 上一条历史
    HISTORY_DOWN,   // 下一条历史
    ARROW_LEFT,     // 左键
    ARROW_RIGHT,    // 右键
    CTRL_C,         // Ctrl+C
    CTRL_D,         // Ctrl+D
    SAVE,           // :w
    QUIT,           // :q
    SAVE_QUIT,      // :wq
    FORCE_QUIT,     // :q!
    HELP,           // :h
    PRINT,          // :p
    EDIT_LINE,      // :e <行号> <内容>
    APPEND,         // :a <内容>
    INSERT,         // :i <行号> <内容>
    DELETE_LINE,    // :d <行号>
    
    // 🚀 新增：鼠标/滚轮事件
    MOUSE_SCROLL_UP,    // 滚轮上 / 双指下滑
    MOUSE_SCROLL_DOWN,  // 滚轮下 / 双指上滑
    MOUSE_CLICK,        // 左键单击

    // 🚀 新增：系统内核专属信号
    SYS_KILL,       // 杀进程 (温和)
    SYS_IO,         // 强杀 (强制)
    SYS_OI,         // 重启进程
    SYS_DR,         // 挂起进入后台

    UNKNOWN             // 未知
};

// ================= 按键输入结构 =================
struct KeyInput {
    KeyEvent event = KeyEvent::NONE;
    std::string raw;        // 原始输入
    std::string text;       // 普通文本内容
    std::string cmd;        // 命令名
    std::string args;       // 命令参数
    int line_num = 0;       // 解析出的行号

    // 🚀 新增：鼠标坐标（仅 MOUSE_* 事件有效，1-based）
    int mouse_x = 0;
    int mouse_y = 0;
};

// ================= 虚拟键盘驱动 =================
class DCLKeyboard {
public:
    static void set_context(KeyContext ctx);
    static KeyContext get_context();

    // 主进程身份检测声明
    static bool is_main_process();

    // 核心接口：读取按键（立即响应，不等待回车）
    static KeyInput read();

    // 纯解析（用于处理已输入的字符串）
    static KeyInput parse(const std::string& input);

    // 编码转换函数声明
    static std::string encode_to_pinyin(const std::string& raw_input);

    // 👇 【新增】提示符设置，用于退格时完美重绘 👇
    static void set_prompt(const std::string& p);

    // 历史记录托管
    static void add_history(const std::string& cmd);
    static void history_up();
    static void history_down();
    static std::string current_history();
    static void reset_history_cursor();
    static const std::vector<std::string>& get_history();

private:
    static KeyContext s_context;
    static std::vector<std::string> s_history;
    static int s_history_index;

    // 👇 【新增】保存当前提示符，例如 ">>> " 或 "~/dir>> " 👇
    static std::string s_prompt;
};