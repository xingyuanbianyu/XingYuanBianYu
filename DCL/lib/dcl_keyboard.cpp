#include "dcl_keyboard.h"
#include <iostream>
#include <algorithm>
#include <unistd.h>
#include <termios.h>
#include <limits.h>
#include <cstring>
#include <vector>
#include <sys/ioctl.h>

KeyContext DCLKeyboard::s_context = KeyContext::MAIN;
std::vector<std::string> DCLKeyboard::s_history;
int DCLKeyboard::s_history_index = 0;

std::string DCLKeyboard::s_prompt = ">>> ";
void DCLKeyboard::set_prompt(const std::string& p) { s_prompt = p; }

bool DCLKeyboard::is_main_process() {
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) return true;
    exe_path[len] = '\0';
    std::string path(exe_path);
    size_t slash_pos = path.find_last_of('/');
    std::string filename = (slash_pos == std::string::npos) ? path : path.substr(slash_pos + 1);
    const std::string expected = "Data_Command_Line";
    if (filename.length() < expected.length()) return false;
    if (filename.compare(0, expected.length(), expected) != 0) return false;
    if (filename.length() == expected.length()) return true;
    if (filename[expected.length()] == '.') return true;
    return false;
}

void DCLKeyboard::set_context(KeyContext ctx) { s_context = ctx; }
KeyContext DCLKeyboard::get_context() { return s_context; }

std::string DCLKeyboard::encode_to_pinyin(const std::string& raw_input) {
    return raw_input;
}

// 【核心结构】逻辑单元，代表一个“字”
struct KeyUnit {
    std::string text;
    int visual;
};

KeyInput DCLKeyboard::read() {
    KeyInput result;
    std::vector<KeyUnit> buffer;
    int cursor_idx = 0;

    int hist_index = s_history.size();
    std::vector<KeyUnit> pre_history_buffer;
    int pre_history_cursor = 0;

    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO | ISIG);
    newt.c_cc[VMIN]  = 1;
    newt.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    bool mouse_mode = (s_context == KeyContext::NANO);
    if (mouse_mode) {
        std::cout << "\033[?1000h\033[?1006h" << std::flush;
    }

    struct Cleanup {
        struct termios* oldt;
        bool*          mouse_mode;
        ~Cleanup() {
            if (mouse_mode && *mouse_mode) {
                std::cout << "\033[?1000l\033[?1006l" << std::flush;
            }
            tcsetattr(STDIN_FILENO, TCSANOW, oldt);
        }
    } cleanup{&oldt, &mouse_mode};

    auto terminal_width = []() -> int {
        struct winsize w;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
            return (int)w.ws_col;
        return 80;
    };

    auto visible_width = [](const std::string& s) -> int {
        int w = 0;
        size_t i = 0;
        while (i < s.size()) {
            if (s[i] == '\033') {
                i++;
                if (i < s.size() && s[i] == '[') {
                    i++;
                    while (i < s.size() &&
                           !((s[i] >= 'A' && s[i] <= 'Z') ||
                             (s[i] >= 'a' && s[i] <= 'z'))) i++;
                    if (i < s.size()) i++;
                }
                continue;
            }
            unsigned char ch = (unsigned char)s[i];
            int len = 1;
            if      ((ch & 0x80) == 0)    len = 1;
            else if ((ch & 0xE0) == 0xC0) len = 2;
            else if ((ch & 0xF0) == 0xE0) len = 3;
            else if ((ch & 0xF8) == 0xF0) len = 4;
            if (i + len > s.size()) break;
            w += (len > 1) ? 2 : 1;
            i += len;
        }
        return w;
    };

    int prev_rows = 1;

    auto redraw = [&]() {
        if (prev_rows > 1) {
            for (int i = 1; i < prev_rows; ++i) std::cout << "\033[A";
        }
        std::cout << "\r\033[J";
        std::cout << s_prompt;
        for (const auto& u : buffer) std::cout << u.text;

        int back = 0;
        for (size_t i = cursor_idx; i < buffer.size(); ++i) back += buffer[i].visual;
        for (int i = 0; i < back; ++i) std::cout << "\033[D";

        int total = visible_width(s_prompt);
        for (const auto& u : buffer) total += u.visual;
        int tw = terminal_width();
        prev_rows = (std::max(1, total) + tw - 1) / tw;

        std::cout.flush();
    };

    redraw();

    auto split_to_units = [](const std::string& s) {
        std::vector<KeyUnit> units;
        for (size_t i = 0; i < s.size(); ) {
            unsigned char ch = (unsigned char)s[i];
            int len = 1;
            if      ((ch & 0x80) == 0)    len = 1;
            else if ((ch & 0xE0) == 0xC0) len = 2;
            else if ((ch & 0xF0) == 0xE0) len = 3;
            else if ((ch & 0xF8) == 0xF0) len = 4;
            if (i + len > s.size()) break;
            units.push_back({s.substr(i, len), (len > 1) ? 2 : 1});
            i += len;
        }
        return units;
    };

    while (true) {
        unsigned char c;
        if (::read(STDIN_FILENO, &c, 1) == -1) break;

        if (c == '\n' || c == '\r') {
            std::cout << "\n";
            if (buffer.empty()) {
                result.event = KeyEvent::NONE;
                return result;
            }
            std::string raw_str = "";
            for (const auto& unit : buffer) raw_str += unit.text;
            result.raw = raw_str;
            return parse(raw_str);
        }
        else if (c == 27) { // ESC 序列
            char seq[2];
            if (::read(STDIN_FILENO, &seq[0], 1) != 1) break;
            if (::read(STDIN_FILENO, &seq[1], 1) != 1) break;

            if (seq[0] == '[') {
                if (seq[1] == '<') {
                    std::string params;
                    unsigned char ch;
                    while (::read(STDIN_FILENO, &ch, 1) == 1) {
                        params += (char)ch;
                        if (ch == 'M' || ch == 'm') break;
                        if (params.size() > 32) break;
                    }
                    bool released = (!params.empty() && params.back() == 'm');
                    if (!params.empty() && (params.back() == 'M' || params.back() == 'm'))
                        params.pop_back();

                    size_t p1 = params.find(';');
                    size_t p2 = (p1 == std::string::npos) ? std::string::npos : params.find(';', p1 + 1);
                    if (p1 != std::string::npos && p2 != std::string::npos) {
                        int button = -1, mx = 0, my = 0;
                        try {
                            button = std::stoi(params.substr(0, p1));
                            mx     = std::stoi(params.substr(p1 + 1, p2 - p1 - 1));
                            my     = std::stoi(params.substr(p2 + 1));
                        } catch (...) { button = -1; }

                        result.mouse_x = mx;
                        result.mouse_y = my;

                        if (button == 64) { result.event = KeyEvent::MOUSE_SCROLL_UP; return result; }
                        if (button == 65) { result.event = KeyEvent::MOUSE_SCROLL_DOWN; return result; }
                        if (button == 0 && !released) { result.event = KeyEvent::MOUSE_CLICK; return result; }
                    }
                    continue;
                }

                if (seq[1] == 'A') {
                    if (hist_index > 0) {
                        if (hist_index == (int)s_history.size()) {
                            pre_history_buffer = buffer;
                            pre_history_cursor = cursor_idx;
                        }
                        hist_index--;
                        buffer = split_to_units(s_history[hist_index]);
                        cursor_idx = buffer.size();
                        redraw();
                    }
                }
                else if (seq[1] == 'B') {
                    if (hist_index < (int)s_history.size()) {
                        hist_index++;
                        if (hist_index < (int)s_history.size()) {
                            buffer = split_to_units(s_history[hist_index]);
                            cursor_idx = buffer.size();
                        } else {
                            buffer = pre_history_buffer;
                            cursor_idx = pre_history_cursor;
                        }
                        redraw();
                    }
                }
                else if (seq[1] == 'D') {
                    if (cursor_idx > 0) { cursor_idx--; redraw(); }
                }
                else if (seq[1] == 'C') {
                    if (cursor_idx < (int)buffer.size()) { cursor_idx++; redraw(); }
                }
                else if (seq[1] == 'H') { cursor_idx = 0; redraw(); }
                else if (seq[1] == 'F') { cursor_idx = buffer.size(); redraw(); }
                else if (seq[1] == '3') {
                    char tail;
                    if (::read(STDIN_FILENO, &tail, 1) != 1) continue;
                    if (tail == '~') {
                        if (cursor_idx < (int)buffer.size()) {
                            buffer.erase(buffer.begin() + cursor_idx);
                            redraw();
                        }
                    }
                }
                else if (seq[1] == '1' || seq[1] == '4' || seq[1] == '7' || seq[1] == '8') {
                    char tail;
                    if (::read(STDIN_FILENO, &tail, 1) != 1) continue;
                    if (tail == '~') {
                        if (seq[1] == '1' || seq[1] == '7') cursor_idx = 0;
                        else                                cursor_idx = buffer.size();
                        redraw();
                    }
                }
            }
        }
        // ============================================================
        // 🚀 核心修改：把控制字符转为自定义信号
        // ============================================================
        else if (c == 11) { // Ctrl+K -> SYS_KILL
            std::cout << "\n";
            result.event = KeyEvent::SYS_KILL;
            return result;
        }
        else if (c == 9) {  // Ctrl+I (Tab) -> SYS_IO
            std::cout << "\n";
            result.event = KeyEvent::SYS_IO;
            return result;
        }
        else if (c == 15) { // Ctrl+O -> SYS_OI
            std::cout << "\n";
            result.event = KeyEvent::SYS_OI;
            return result;
        }
        else if (c == 18) { // Ctrl+R -> SYS_DR
            std::cout << "\n";
            result.event = KeyEvent::SYS_DR;
            return result;
        }
        // ============================================================
        else if (c == 3 || c == 4 || c == 26) {
            continue; // 忽略 Ctrl+C, Ctrl+D, Ctrl+Z
        }
        else if (c == 127 || c == 8) { // Backspace
            if (cursor_idx > 0) {
                buffer.erase(buffer.begin() + cursor_idx - 1);
                cursor_idx--;
                redraw();
            }
        }
        else {
            hist_index = s_history.size();
            std::string char_str(1, (char)c);
            int visual = 1;

            if ((c & 0xE0) == 0xC0) {
                visual = 2;
                char_str.resize(2);
                ::read(STDIN_FILENO, &char_str[1], 1);
            }
            else if ((c & 0xF0) == 0xE0) {
                visual = 2;
                char_str.resize(3);
                ::read(STDIN_FILENO, &char_str[1], 1);
                ::read(STDIN_FILENO, &char_str[2], 1);
            }
            else if ((c & 0xF8) == 0xF0) {
                visual = 2;
                char_str.resize(4);
                for (int i = 1; i < 4; ++i) ::read(STDIN_FILENO, &char_str[i], 1);
            }

            buffer.insert(buffer.begin() + cursor_idx, {char_str, visual});
            cursor_idx++;
            redraw();
        }
    }

    return result;
}

// ================= 解析函数 =================
KeyInput DCLKeyboard::parse(const std::string& input) {
    KeyInput result;
    result.raw = input;
    if (input.empty()) return result;
    
    // ============================================================
    // 🚀 核心拦截：如果识别到全大写的自定义指令，直接转换
    // ============================================================
    if (input == "KILL") {
        result.event = KeyEvent::SYS_KILL; return result;
    }
    if (input == "IO") {
        result.event = KeyEvent::SYS_IO; return result;
    }
    if (input == "OI") {
        result.event = KeyEvent::SYS_OI; return result;
    }
    if (input == "DR") {
        result.event = KeyEvent::SYS_DR; return result;
    }

    if (input[0] == ':') {
        std::string cmd_line = input.substr(1);
        size_t sp = cmd_line.find(' ');
        if (sp == std::string::npos) result.cmd = cmd_line;
        else { result.cmd = cmd_line.substr(0, sp); result.args = cmd_line.substr(sp + 1); }
        if (s_context == KeyContext::NANO) {
            if (result.cmd == "w") result.event = KeyEvent::SAVE;
            else if (result.cmd == "q") result.event = KeyEvent::QUIT;
            else if (result.cmd == "wq") result.event = KeyEvent::SAVE_QUIT;
            else if (result.cmd == "q!") result.event = KeyEvent::FORCE_QUIT;
            else if (result.cmd == "shutdown" || result.cmd == "poweroff" || result.cmd == "halt") result.event = KeyEvent::FORCE_QUIT;
            else if (result.cmd == "reboot") result.event = KeyEvent::FORCE_QUIT;
            else if (result.cmd == "h") result.event = KeyEvent::HELP;
            else if (result.cmd == "p") result.event = KeyEvent::PRINT;
            else if (result.cmd == "a") result.event = KeyEvent::APPEND;
            else if (result.cmd == "e") {
                result.event = KeyEvent::EDIT_LINE;
                size_t sp2 = result.args.find(' ');
                if (sp2 != std::string::npos) {
                    try { result.line_num = std::stoi(result.args.substr(0, sp2)); result.args = result.args.substr(sp2+1); }
                    catch (...) { result.event = KeyEvent::UNKNOWN; }
                } else result.event = KeyEvent::UNKNOWN;
            }
            else if (result.cmd == "i") {
                result.event = KeyEvent::INSERT;
                size_t sp2 = result.args.find(' ');
                if (sp2 != std::string::npos) {
                    try { result.line_num = std::stoi(result.args.substr(0, sp2)); result.args = result.args.substr(sp2+1); }
                    catch (...) { result.event = KeyEvent::UNKNOWN; }
                } else result.event = KeyEvent::UNKNOWN;
            }
            else if (result.cmd == "d") {
                result.event = KeyEvent::DELETE_LINE;
                try { result.line_num = std::stoi(result.args); } catch (...) { result.event = KeyEvent::UNKNOWN; }
            }
            else result.event = KeyEvent::UNKNOWN;
        } else {
            result.event = KeyEvent::COMMAND;
        }
        return result;
    }
    result.event = KeyEvent::TEXT;
    result.text = input;
    return result;
}

void DCLKeyboard::add_history(const std::string& cmd) {
    if (s_history.empty() || s_history.back() != cmd) s_history.push_back(cmd);
    s_history_index = s_history.size();
}
void DCLKeyboard::history_up() { if (s_history_index > 0) s_history_index--; }
void DCLKeyboard::history_down() { if (s_history_index < (int)s_history.size()) s_history_index++; }
std::string DCLKeyboard::current_history() { return (s_history_index < (int)s_history.size()) ? s_history[s_history_index] : ""; }
void DCLKeyboard::reset_history_cursor() { s_history_index = s_history.size(); }
const std::vector<std::string>& DCLKeyboard::get_history() { return s_history; }