#include "dcl_nano.h"
#include "dcl_kill.h"
#include "../../DCL/lib/dcl_keyboard.h"
#include "../../VFS/include/vfs.h"
#include "../../AMM/include/amm_memory.h"
#include "../../DD/include/mem_driver.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstring>
#include <poll.h>
#include <signal.h>
#include <errno.h>

extern int current_v_pid;

// ================= 🚀 SIGWINCH 全局标志 =================
static volatile sig_atomic_t g_winch = 0;
static void on_winch(int) { g_winch = 1; }

// ================= VFS 读写辅助 =================
static std::vector<std::string> read_lines_from_vfs(const std::string& vfs_path) {
    std::vector<std::string> lines;

    // 🚀 【核心修复 1】先用 get_node 确认文件存在，避免把 "错误" 前缀误判为读取失败
    auto node = VFS::getInstance().get_node(vfs_path);
    if (!node) return lines;                                        // 文件不存在
    if (std::dynamic_pointer_cast<Directory>(node)) return lines;    // 是目录

    std::string content = VFS::getInstance().read_file(vfs_path);

    // 🚀 空文件是合法的，直接返回空行向量
    if (content.empty()) return lines;

    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) lines.push_back(line);
    if (!content.empty() && content.back() == '\n' && !lines.empty() && lines.back().empty())
        lines.pop_back();
    return lines;
}

// 🚀 保存：返回空字符串=成功，非空=错误信息
static std::string write_lines_to_vfs(const std::string& vfs_path,
                                       const std::vector<std::string>& lines) {
    std::string content;
    for (size_t i = 0; i < lines.size(); ++i) {
        content += lines[i];
        if (i + 1 < lines.size()) content += "\n";
    }
    // VFS::write_file 返回空=成功，非空=错误信息
    return VFS::getInstance().write_file(vfs_path, content);
}

// ================= 屏幕 / UTF-8 辅助 =================
static void get_term_size(int& w, int& h) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        w = ws.ws_col; h = ws.ws_row;
    } else { w = 80; h = 24; }
}

static int display_width(const std::string& s, int n) {
    int w = 0, cnt = 0, i = 0;
    while (i < (int)s.size() && cnt < n) {
        unsigned char ch = s[i];
        int len = 1;
        if      ((ch & 0x80) == 0)    len = 1;
        else if ((ch & 0xE0) == 0xC0) len = 2;
        else if ((ch & 0xF0) == 0xE0) len = 3;
        else if ((ch & 0xF8) == 0xF0) len = 4;
        if (i + len > (int)s.size()) break;
        w += (len > 1) ? 2 : 1;
        i += len; cnt++;
    }
    return w;
}

static int char_count(const std::string& s) {
    int cnt = 0, i = 0;
    while (i < (int)s.size()) {
        unsigned char ch = s[i];
        int len = 1;
        if      ((ch & 0x80) == 0)    len = 1;
        else if ((ch & 0xE0) == 0xC0) len = 2;
        else if ((ch & 0xF0) == 0xE0) len = 3;
        else if ((ch & 0xF8) == 0xF0) len = 4;
        i += len; cnt++;
    }
    return cnt;
}

static int byte_offset(const std::string& s, int n) {
    int cnt = 0, i = 0;
    while (i < (int)s.size() && cnt < n) {
        unsigned char ch = s[i];
        int len = 1;
        if      ((ch & 0x80) == 0)    len = 1;
        else if ((ch & 0xE0) == 0xC0) len = 2;
        else if ((ch & 0xF0) == 0xE0) len = 3;
        else if ((ch & 0xF8) == 0xF0) len = 4;
        i += len; cnt++;
    }
    return i;
}

static std::string read_utf8_char(unsigned char c) {
    std::string s(1, (char)c);
    if ((c & 0xE0) == 0xC0) {
        s.resize(2); ::read(STDIN_FILENO, &s[1], 1);
    } else if ((c & 0xF0) == 0xE0) {
        s.resize(3); ::read(STDIN_FILENO, &s[1], 1); ::read(STDIN_FILENO, &s[2], 1);
    } else if ((c & 0xF8) == 0xF0) {
        s.resize(4); for (int i = 1; i < 4; ++i) ::read(STDIN_FILENO, &s[i], 1);
    }
    return s;
}

// ================= 主入口 =================
std::string nano_cmd(DCLState& state, const std::string& vfs_path) {
    if (vfs_path.find('\n') != std::string::npos || vfs_path.find('\r') != std::string::npos)
        return "错误: 编辑器路径包含换行符，拒绝打开";

    // 🚀 【核心修复 2】nano 自身的权限校验（防御性，防止被非 open 路径调用）
    if (!VFS::getInstance().check_resource_access(vfs_path)) {
        return "权限不足: 无法编辑该文件 (" + vfs_path + ")";
    }

    // 🚀 目标不能是目录
    auto target_node = VFS::getInstance().get_node(vfs_path);
    if (target_node && std::dynamic_pointer_cast<Directory>(target_node)) {
        return "错误: 目标是一个目录，无法编辑";
    }

    std::vector<std::string> lines = read_lines_from_vfs(vfs_path);
    if (lines.empty()) lines.push_back("");
    bool modified = false;
    bool running  = true;
    bool force_killed = false;

    int cx = 0;
    int cy = 0;
    int scroll = 0;
    std::string cut_buffer;
    std::string status_msg;

    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO | ISIG);
    newt.c_cc[VMIN]  = 1;
    newt.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    struct sigaction sa;
    sa.sa_handler = on_winch;
    sa.sa_flags   = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, nullptr);
    g_winch = 0;

    std::cout << "\033[?1049h\033[?25l" << std::flush;

    std::string display_path = VFS::getInstance().get_display_path(vfs_path);

    auto render = [&]() {
        int W, H;
        get_term_size(W, H);
        int edit_h = H - 3;
        if (edit_h < 1) edit_h = 1;

        if (cy < scroll) scroll = cy;
        if (cy >= scroll + edit_h) scroll = cy - edit_h + 1;
        if (scroll < 0) scroll = 0;

        std::ostringstream out;

        std::string title = "  GNU nano 1.0    " + display_path
                          + (modified ? "  [已修改]" : "");
        out << "\033[H\033[7m";
        if ((int)title.size() > W) title = title.substr(0, W);
        out << title;
        for (size_t i = title.size(); i < (size_t)W; ++i) out << ' ';
        out << "\033[0m";

        for (int i = 0; i < edit_h; ++i) {
            out << "\033[" << (i + 2) << ";1H\033[K";
            int li = scroll + i;
            if (li < (int)lines.size()) {
                std::string text = lines[li];
                int w = 0, j = 0;
                while (j < (int)text.size() && w < W) {
                    unsigned char ch = text[j];
                    int len = 1;
                    if      ((ch & 0x80) == 0)    len = 1;
                    else if ((ch & 0xE0) == 0xC0) len = 2;
                    else if ((ch & 0xF0) == 0xE0) len = 3;
                    else if ((ch & 0xF8) == 0xF0) len = 4;
                    if (j + len > (int)text.size()) break;
                    w += (len > 1) ? 2 : 1;
                    j += len;
                }
                out << text.substr(0, j);
            }
        }

        std::string status = status_msg;
        if (status.empty()) {
            status = "  [行 " + std::to_string(cy + 1) + "/" + std::to_string(lines.size())
                   + ", 列 " + std::to_string(cx + 1) + "]";
        }
        out << "\033[" << (H - 1) << ";1H\033[7m";
        if ((int)status.size() > W) status = status.substr(0, W);
        out << status;
        for (size_t i = status.size(); i < (size_t)W; ++i) out << ' ';

        std::string help = "  ^G 帮助  ^O 保存  ^X 退出  ^K 剪切  ^U 粘贴  ^Y 上翻  ^V 下翻";
        out << "\033[" << H << ";1H";
        if ((int)help.size() > W) help = help.substr(0, W);
        out << help;
        for (size_t i = help.size(); i < (size_t)W; ++i) out << ' ';
        out << "\033[0m";

        int screen_row = 2 + (cy - scroll);
        int screen_col = 1 + display_width(lines[cy], cx);
        if (screen_row < 2) screen_row = 2;
        if (screen_row >= H - 1) screen_row = H - 2;
        if (screen_col > W) screen_col = W;
        out << "\033[" << screen_row << ";" << screen_col << "H";
        out << "\033[?25h";
        std::cout << out.str() << std::flush;
    };

    auto show_help_screen = [&]() {
        int W, H;
        get_term_size(W, H);
        std::ostringstream out;
        out << "\033[2J\033[H";
        out << "  ╔══════════ 星缘编辑器帮助 ══════════╗\n";
        out << "  ║  ^G   显示本帮助                  ║\n";
        out << "  ║  ^O   保存文件                    ║\n";
        out << "  ║  ^X   退出 (有修改会提示)         ║\n";
        out << "  ║  ^K   剪切当前行                  ║\n";
        out << "  ║  ^U   粘贴到光标处                ║\n";
        out << "  ║  ^Y / ^V   上翻页 / 下翻页        ║\n";
        out << "  ║  方向键    移动光标               ║\n";
        out << "  ║  Home/End  行首 / 行尾            ║\n";
        out << "  ║  Enter     换行                   ║\n";
        out << "  ║  Backspace 删除字符               ║\n";
        out << "  ║  Delete    删除光标处字符         ║\n";
        out << "  ╚═══════════════════════════════════╝\n";
        out << "\n  按任意键返回...";
        std::cout << out.str() << std::flush;
        unsigned char any;
        ::read(STDIN_FILENO, &any, 1);
        std::cout << "\033[2J\033[H" << std::flush;
    };

    render();

    int last_w = 0, last_h = 0;
    get_term_size(last_w, last_h);

    while (running) {
        auto it = process_table.find(current_v_pid);
        if (it != process_table.end() && it->second.pending_kill) {
            force_killed = true;
            running = false;
            break;
        }

        struct pollfd pfd;
        pfd.fd     = STDIN_FILENO;
        pfd.events = POLLIN;

        int pr = poll(&pfd, 1, 200);

        if (g_winch) {
            g_winch = 0;
            get_term_size(last_w, last_h);
            render();
            continue;
        }

        if (pr == 0) {
            int W, H;
            get_term_size(W, H);
            if (W != last_w || H != last_h) {
                last_w = W; last_h = H;
                render();
            }
            continue;
        }

        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }

        unsigned char c;
        if (::read(STDIN_FILENO, &c, 1) != 1) break;

        status_msg.clear();

        if (c == 27) {
            unsigned char s1, s2;
            if (::read(STDIN_FILENO, &s1, 1) != 1) continue;
            if (::read(STDIN_FILENO, &s2, 1) != 1) continue;
            if (s1 != '[') continue;

            switch (s2) {
                case 'A': if (cy > 0) cy--; if (cx > char_count(lines[cy])) cx = char_count(lines[cy]); break;
                case 'B': if (cy + 1 < (int)lines.size()) cy++; if (cx > char_count(lines[cy])) cx = char_count(lines[cy]); break;
                case 'C': if (cx < char_count(lines[cy])) cx++; break;
                case 'D': if (cx > 0) cx--; break;
                case 'H': cx = 0; break;
                case 'F': cx = char_count(lines[cy]); break;
                case '1': case '4': case '7': case '8': {
                    unsigned char s3;
                    if (::read(STDIN_FILENO, &s3, 1) != 1) break;
                    if (s2 == '1' || s2 == '7') cx = 0;
                    else                        cx = char_count(lines[cy]);
                    break;
                }
                case '3': {
                    unsigned char s3;
                    if (::read(STDIN_FILENO, &s3, 1) != 1) break;
                    if (s3 != '~') break;
                    int n = char_count(lines[cy]);
                    if (cx < n) {
                        int b = byte_offset(lines[cy], cx);
                        int len = byte_offset(lines[cy], cx + 1) - b;
                        lines[cy].erase(b, len);
                        modified = true;
                    } else if (cy + 1 < (int)lines.size()) {
                        lines[cy] += lines[cy + 1];
                        lines.erase(lines.begin() + cy + 1);
                        modified = true;
                    }
                    break;
                }
                case '5': {
                    unsigned char s3;
                    if (::read(STDIN_FILENO, &s3, 1) != 1) break;
                    int Wt, Ht; get_term_size(Wt, Ht);
                    int page = (Ht > 3) ? Ht - 4 : 20;
                    cy -= page; if (cy < 0) cy = 0;
                    if (cx > char_count(lines[cy])) cx = char_count(lines[cy]);
                    break;
                }
                case '6': {
                    unsigned char s3;
                    if (::read(STDIN_FILENO, &s3, 1) != 1) break;
                    int Wt, Ht; get_term_size(Wt, Ht);
                    int page = (Ht > 3) ? Ht - 4 : 20;
                    cy += page; if (cy >= (int)lines.size()) cy = (int)lines.size() - 1;
                    if (cx > char_count(lines[cy])) cx = char_count(lines[cy]);
                    break;
                }
            }
            render();
            continue;
        }

        if (c == 127 || c == 8) {
            if (cx > 0) {
                int b = byte_offset(lines[cy], cx - 1);
                int len = byte_offset(lines[cy], cx) - b;
                lines[cy].erase(b, len);
                cx--;
                modified = true;
            } else if (cy > 0) {
                cx = char_count(lines[cy - 1]);
                lines[cy - 1] += lines[cy];
                lines.erase(lines.begin() + cy);
                cy--;
                modified = true;
            }
            render();
            continue;
        }

        if (c == '\n' || c == '\r') {
            int b = byte_offset(lines[cy], cx);
            std::string rest = lines[cy].substr(b);
            lines[cy] = lines[cy].substr(0, b);
            lines.insert(lines.begin() + cy + 1, rest);
            cy++; cx = 0;
            modified = true;
            render();
            continue;
        }

        if (c == '\t') {
            int b = byte_offset(lines[cy], cx);
            lines[cy].insert(b, "    ");
            cx += 4;
            modified = true;
            render();
            continue;
        }

        if (c == 7)  { show_help_screen(); render(); continue; }

        // ============================================================
        // 🚀 Ctrl+O 保存
        // ============================================================
        if (c == 15) {
            std::string content;
            for (size_t i = 0; i < lines.size(); ++i) {
                content += lines[i];
                if (i + 1 < lines.size()) content += "\n";
            }

            std::string save_res = write_lines_to_vfs(vfs_path, lines);

            if (save_res.empty()) {
                modified = false;
                status_msg = "  [已保存 " + std::to_string(lines.size())
                           + " 行, " + std::to_string(content.size()) + " 字节]";
            } else {
                status_msg = "  [保存失败: " + save_res + "]";
            }
            render();
            continue;
        }

        // 🚀 Ctrl+X 退出
        if (c == 24) {
            if (modified) {
                int W, H;
                get_term_size(W, H);
                std::string prompt = " 保存已修改的缓冲区？ (Y) 是  (N) 否  (C) 取消 ";
                std::ostringstream out;
                out << "\033[" << (H - 1) << ";1H\033[7m";
                if ((int)prompt.size() > W) prompt = prompt.substr(0, W);
                out << prompt;
                for (size_t i = prompt.size(); i < (size_t)W; ++i) out << ' ';
                out << "\033[0m" << std::flush;
                std::cout << out.str();

                unsigned char ans;
                if (::read(STDIN_FILENO, &ans, 1) != 1) continue;
                if (ans == 'Y' || ans == 'y') {
                    std::string save_res = write_lines_to_vfs(vfs_path, lines);
                    if (save_res.empty()) {
                        modified = false;
                        running = false;
                    } else {
                        status_msg = "  [保存失败: " + save_res + "]";
                        render();
                        continue;
                    }
                } else if (ans == 'N' || ans == 'n') {
                    running = false;
                }
            } else {
                running = false;
            }
            if (!running) break;
            render();
            continue;
        }
        if (c == 11) {
            cut_buffer = lines[cy];
            if (lines.size() == 1) lines[0].clear();
            else {
                lines.erase(lines.begin() + cy);
                if (cy >= (int)lines.size()) cy = (int)lines.size() - 1;
            }
            cx = 0;
            modified = true;
            status_msg = "  [已剪切 1 行]";
            render();
            continue;
        }
        if (c == 21) {
            if (!cut_buffer.empty()) {
                lines.insert(lines.begin() + cy, cut_buffer);
                cy++; cx = 0;
                modified = true;
                status_msg = "  [已粘贴]";
            }
            render();
            continue;
        }
        if (c == 25) {
            int W, H; get_term_size(W, H);
            int page = (H > 3) ? H - 4 : 20;
            cy -= page; if (cy < 0) cy = 0;
            if (cx > char_count(lines[cy])) cx = char_count(lines[cy]);
            render();
            continue;
        }
        if (c == 22) {
            int W, H; get_term_size(W, H);
            int page = (H > 3) ? H - 4 : 20;
            cy += page; if (cy >= (int)lines.size()) cy = (int)lines.size() - 1;
            if (cx > char_count(lines[cy])) cx = char_count(lines[cy]);
            render();
            continue;
        }
        if (c == 3) {
            status_msg = "  [行 " + std::to_string(cy + 1)
                       + ", 列 " + std::to_string(cx + 1) + "]";
            render();
            continue;
        }

        if (c >= 32) {
            std::string ch = read_utf8_char(c);
            int b = byte_offset(lines[cy], cx);
            lines[cy].insert(b, ch);
            cx++;
            modified = true;
            render();
            continue;
        }
    }

    signal(SIGWINCH, SIG_DFL);

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

    std::cout << "\033[?1049l\033[?25h" << std::flush;

    if (!force_killed) {
        state.data = "";
        for (size_t i = 0; i < lines.size(); ++i) {
            state.data += lines[i];
            if (i + 1 < lines.size()) state.data += "\n";
        }
        state.path = vfs_path;
        state.cmd  = "nano";
    } else {
        state.cmd = "nano (强制中断)";
    }

    return "";
}