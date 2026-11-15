#pragma once
#include <string>

// ============================================================
// 星缘OS · 窗口接口
// ============================================================
class IWindow {
public:
    virtual ~IWindow() = default;

    virtual bool create(const std::string& title, int width, int height) = 0;
    virtual void submit(const std::string& content) = 0;
    virtual void flush() = 0;
    virtual bool poll() = 0;
    virtual void destroy() = 0;

    // 【新增】是否为交互式窗口
    //   终端降级实现返回 false（直接输出即可，无需后台线程）
    //   真实窗口（Win32/Android）返回 true（需要后台线程维护）
    virtual bool is_interactive() const = 0;
};

IWindow* create_window();