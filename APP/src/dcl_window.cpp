#include "../include/dcl_window.h"
#include <iostream>
#include <string>
#include <vector>

// ============================================================
// ANSI 解析工具（给不支持 ANSI 的平台用）
// ============================================================
struct TextSegment {
    std::string text;
    int r = 0, g = 0, b = 0;
    bool is_bold = false;
};

static std::vector<TextSegment> parse_ansi(const std::string& content) {
    std::vector<TextSegment> segments;
    TextSegment current;
    size_t i = 0;
    while (i < content.size()) {
        if (content[i] == '\033') {
            if (!current.text.empty()) {
                segments.push_back(current);
                current.text.clear();
            }
            size_t end = content.find('m', i);
            if (end == std::string::npos) { i++; continue; }
            std::string seq = content.substr(i + 2, end - i - 2);
            i = end + 1;
            if (seq == "0") {
                current.r = current.g = current.b = 0;
                current.is_bold = false;
            } else if (seq == "1") {
                current.is_bold = true;
            } else if (seq.rfind("38;2;", 0) == 0) {
                int r, g, b;
                if (sscanf(seq.c_str(), "38;2;%d;%d;%d", &r, &g, &b) == 3) {
                    current.r = r; current.g = g; current.b = b;
                }
            }
            continue;
        }
        current.text += content[i];
        i++;
    }
    if (!current.text.empty()) segments.push_back(current);
    return segments;
}

// ============================================================
// 平台 1：终端降级（Termux / Linux / macOS）
// ============================================================
class TerminalWindow : public IWindow {
public:
    bool create(const std::string&, int, int) override { return true; }
    void submit(const std::string& content) override { std::cout << content; }
    void flush() override { std::cout << std::flush; }
    bool poll() override { return true; }
    void destroy() override {}

    // 【核心】终端不是交互窗口，不需要后台线程
    bool is_interactive() const override { return false; }
};

// ============================================================
// 平台 2：Windows 原生窗口
// ============================================================
#ifdef _WIN32
#include <windows.h>

LRESULT CALLBACK XybyWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, m, w, l);
}

class Win32Window : public IWindow {
    HWND m_hwnd = nullptr;
    HDC  m_hdc  = nullptr;
    bool m_running = true;
    std::string m_content;

public:
    bool create(const std::string& title, int w, int h) override {
        HINSTANCE inst = GetModuleHandle(nullptr);
        WNDCLASS wc = {0};
        wc.lpfnWndProc   = XybyWndProc;
        wc.hInstance     = inst;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "XingYuanOSWindow";
        RegisterClass(&wc);

        m_hwnd = CreateWindowA("XingYuanOSWindow", title.c_str(),
                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                               w, h, nullptr, nullptr, inst, nullptr);
        if (!m_hwnd) return false;

        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
        m_hdc = GetDC(m_hwnd);
        return true;
    }

    void submit(const std::string& content) override { m_content = content; }

    void flush() override {
        if (!m_hdc) return;
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        FillRect(m_hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
        SetBkMode(m_hdc, TRANSPARENT);

        auto segments = parse_ansi(m_content);
        int x = 10, y = 10;
        for (const auto& seg : segments) {
            std::string line;
            for (char c : seg.text) {
                if (c == '\n') {
                    SetTextColor(m_hdc, RGB(seg.r, seg.g, seg.b));
                    TextOutA(m_hdc, x, y, line.c_str(), (int)line.size());
                    line.clear();
                    y += 20; x = 10;
                } else {
                    line += c;
                }
            }
            if (!line.empty()) {
                SetTextColor(m_hdc, RGB(seg.r, seg.g, seg.b));
                TextOutA(m_hdc, x, y, line.c_str(), (int)line.size());
                SIZE sz;
                GetTextExtentPoint32A(m_hdc, line.c_str(), (int)line.size(), &sz);
                x += sz.cx;
            }
        }
        m_content.clear();
    }

    bool poll() override {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { m_running = false; return false; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return m_running;
    }

    void destroy() override {
        if (m_hdc)  ReleaseDC(m_hwnd, m_hdc);
        if (m_hwnd) DestroyWindow(m_hwnd);
        m_hwnd = nullptr; m_hdc = nullptr;
    }

    // 【核心】真实窗口需要后台线程维护
    bool is_interactive() const override { return true; }
};
#endif

// ============================================================
// 工厂
// ============================================================
IWindow* create_window() {
#ifdef _WIN32
    return new Win32Window();
#else
    return new TerminalWindow();
#endif
}