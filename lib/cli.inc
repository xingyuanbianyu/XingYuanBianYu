// =================================================================
// cli.inc —— CLI 主循环与命令分发
// =================================================================
#include "../APP/include/dcl_net.h"
#include "../include/dcl_exec.h"
#include "../APP/include/dcl_import.h" // 🚀 引入 import 指令头文件
#include "../APP/include/dcl_xyp4.h"   // 🚀 引入 xyp4 转换指令头文件
#include <sys/wait.h>
#include <unistd.h>
#include <glob.h>
#include <set>
#include <map>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <functional>
#include <termios.h>

extern std::string g_sudo_pass;

std::string net_cmd(const std::vector<std::string>& args);
std::string drt_cmd(DCLState& state, const std::vector<std::string>& args);

// 🚀 修复 1：前置声明改成接收 vector 的新版本，对齐 dcl_exec.h
std::string exec_cmd(DCLState& state, const std::vector<std::string>& cmd_args,
                     std::function<void(const std::string&)> xyn_executor);

std::string open_cmd(DCLState& state, const std::vector<std::string>& raw_args,
                     std::function<void(const std::string&)> xyn_executor);
bool        drt_verify_r3_password(const std::string& input);
std::string drt_get_r3_name();
bool resolve_lib_command(const std::string& cmd, std::string& out_path);
void load_xyn_to_cli(const std::string& vfs_path,
                     const std::vector<std::string>& script_args,
                     const std::function<void(const std::string&)>& cli_executor);

void cli_execute_single(const std::string& cmd_line, DCLState& state, int& cli_priv);

static std::vector<std::string> split_by_and(const std::string& line) {
    std::vector<std::string> segs;
    std::string cur;
    bool in_quote = false;
    char quote_char = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_quote) {
            if (c == quote_char) in_quote = false;
            cur += c;
        } else {
            if (c == '"' || c == '\'') {
                in_quote = true;
                quote_char = c;
                cur += c;
            } else if (c == '&' && i + 1 < line.size() && line[i + 1] == '&') {
                segs.push_back(cur);
                cur.clear();
                i++;
            } else {
                cur += c;
            }
        }
    }
    if (!cur.empty()) segs.push_back(cur);
    return segs;
}

static std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string cur;
    bool in_quote = false;
    char quote_char = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_quote) {
            cur += c;
            if (c == quote_char) in_quote = false;
        } else {
            if (c == '"' || c == '\'') {
                in_quote = true;
                quote_char = c;
                cur += c;
            } else if (c == ' ' || c == '\t') {
                if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
            } else {
                cur += c;
            }
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

static bool is_pre_cmd(const std::string& tok) {
    return tok == "xyby" || tok == "sudo";
}
static bool is_front_cmd(const std::string& tok) {
    return tok == "del" || tok == "kill" || tok == "ioq";
}
static bool is_back_cmd(const std::string& tok) {
    return tok == "open" || tok == "input" || tok == "sft" || tok == "drt";
}

std::string read_password_silent(const std::string& prompt) {
    std::cout << prompt;
    std::cout.flush();
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    std::string pass;
    char c;
    while (::read(STDIN_FILENO, &c, 1) == 1) {
        if (c == '\n' || c == '\r') break;
        if (c == 127 || c == 8) { if (!pass.empty()) pass.pop_back(); continue; }
        pass += c;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::cout << "\n";
    return pass;
}

void dispatch_command(const std::string& cmd_name, const std::vector<std::string>& cmd_args, DCLState& state, int& cli_priv) {
    std::function<void(const std::string&)> execute_single =
        [&state, &cli_priv](const std::string& c) { cli_execute_single(c, state, cli_priv); };

    // 🚀 将 import 和 xyp4 加入已知命令列表
    static const std::set<std::string> known_cmds = {
        "open", "sft", "input", "sound", "player", "net", "run", "kill", "ioq", "ui", "del", "exec", "drt", "import", "xyp4"
    };
    
    if (cmd_name == "SYS_KILL" || cmd_name == "SYS_IO" || cmd_name == "SYS_OI" || cmd_name == "SYS_DR") {
        if (cmd_name == "SYS_KILL") { ioq_cmd(state, {"-9", "0"}); }
        else if (cmd_name == "SYS_IO") { ioq_cmd(state, {"-9", "all"}); }
        else if (cmd_name == "SYS_OI") { ioq_cmd(state, {"0"}); }
        return;
    }

    if (known_cmds.count(cmd_name)) {
        if (cmd_name == "kill" || cmd_name == "ioq") {
            if (cli_priv > 1) { std::cout << "权限不足: " << cmd_name << " 需要内核权限\n"; return; }
        }
        if (!VFS::getInstance().check_cmd_permission(cmd_name)) return;

        std::string r;
        if (cmd_name == "open") { r = open_cmd(state, cmd_args, execute_single); }
        else if (cmd_name == "sft") { r = sft_cmd(state, cmd_args); }
        else if (cmd_name == "del") { r = del_cmd(state, cmd_args); }
        else if (cmd_name == "input") { r = input_cmd(cmd_args); }
        else if (cmd_name == "sound") { r = sound_cmd(cmd_args); }
        else if (cmd_name == "player") { r = play_cmd(cmd_args); }
        else if (cmd_name == "net") { r = net_cmd(cmd_args); }
        else if (cmd_name == "run")  { r = run_cmd(state, cmd_args); }
        else if (cmd_name == "ioq")  { r = ioq_cmd(state, cmd_args); }
        else if (cmd_name == "kill") { r = kill_cmd(state, cmd_args); }
        else if (cmd_name == "drt")  { r = drt_cmd(state, cmd_args); }
        else if (cmd_name == "import") { r = import_cmd(state, cmd_args); }
        else if (cmd_name == "xyp4")   { r = xyp4_cmd(cmd_args); } // 🚀 转发给 xyp4_cmd
        else if (cmd_name == "ui")   { start_ui(state); return; }
        
        // 🚀 修复 2：直接传整个 cmd_args 数组，不再截取 cmd_args[0]
        else if (cmd_name == "exec") { 
            r = exec_cmd(state, cmd_args, execute_single); 
        }

        // 🚀 防泄漏机制：如果是特殊退出信号，不要打印出来污染屏幕，直接交给上层逻辑
        if (r == "__SYS_EXIT__" || r == "__FRONTEND_EXIT__") {
            std::cout << r; // 不换行，直接吐出给截获逻辑抓取
        } else if (!r.empty()) {
            std::cout << r << "\n";
        }
        return;
    }

    // 🚀 这里直接调用 resolve_lib_command，我们在 main_helpers.inc 里已经把它改成了从内存表极速查询
    std::string lib_path;
    if (resolve_lib_command(cmd_name, lib_path)) {
        if (!VFS::getInstance().get_node(lib_path)) { std::cout << "错误: VFS 里找不到 " << lib_path << "\n"; return; }
        if (lib_path.size() >= 4 && lib_path.compare(lib_path.size() - 4, 4, ".xyn") == 0) {
            load_xyn_to_cli(lib_path, cmd_args, execute_single);
            return;
        }
        std::string code = VFS::getInstance().read_file(lib_path);
        std::string header;
        for (size_t k = 0; k < cmd_args.size(); ++k) {
            header += "arg" + std::to_string(k + 1) + " = \"";
            for (char c : cmd_args[k]) {
                if (c == '"' || c == '\\' || c == '\n' || c == '\r') header += '\\';
                header += c;
            }
            header += "\"\n";
        }
        std::set<std::string> included;
        std::string full = expand_includes(header + code, included);
        try { execute_xy_code(full); } catch (...) {}
        return;
    }

    std::string prog_path = CommandRegistry::getInstance().find(cmd_name);
    if (prog_path.empty() && cmd_name.find('/') != std::string::npos) {
        if (access(cmd_name.c_str(), X_OK) == 0) prog_path = cmd_name;
    }
    if (!prog_path.empty()) {
        std::vector<std::string> expanded;
        expanded.push_back(prog_path);
        for (const auto& a : cmd_args) expanded.push_back(a);
        std::vector<char*> argv_c;
        for (auto& a : expanded) argv_c.push_back(const_cast<char*>(a.c_str()));
        argv_c.push_back(nullptr);

        pid_t pid = fork();
        if (pid == 0) {
            execv(prog_path.c_str(), argv_c.data());
            _exit(127);
        } else if (pid > 0) {
            int status = 0; waitpid(pid, &status, 0);
        }
        return;
    }

    std::string full_code = cmd_name;
    for (const auto& a : cmd_args) full_code += " " + a;
    try { execute_xy_code(full_code); } catch (...) {}
}

void cli_execute_single(const std::string& cmd_line, DCLState& state, int& cli_priv) {
    auto segs = split_by_and(cmd_line);
    if (segs.size() > 1) {
        for (const auto& seg : segs) cli_execute_single(seg, state, cli_priv);
        return;
    }

    auto tokens = tokenize(cmd_line);
    if (tokens.empty()) return;

    if (is_pre_cmd(tokens[0])) {
        std::string pre_cmd = tokens[0];
        std::string remaining;
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (i > 1) remaining += " ";
            remaining += tokens[i];
        }
        if (remaining.empty()) return;

        if (pre_cmd == "xyby") {
            int saved_priv = cli_priv;
            int saved_user = VFS::getInstance().get_user_id();
            UserRole saved_role = VFS::getInstance().get_role();
            cli_priv = 0;
            VFS::getInstance().set_user_id(-1);
            VFS::getInstance().set_role(UserRole::R0_ULTIMATE);
            cli_execute_single(remaining, state, cli_priv);
            cli_priv = saved_priv;
            VFS::getInstance().set_user_id(saved_user);
            VFS::getInstance().set_role(saved_role);
            return;
        }
        if (pre_cmd == "sudo") {
            std::string input_pass = g_sudo_pass;
            g_sudo_pass = "";
            if (input_pass.empty()) { std::cout << "错误: 缺失 sudo 密码\n"; return; }
            if (!drt_verify_r3_password(input_pass)) { std::cout << "错误: R3 验证密码不正确\n"; return; }
            
            int saved_priv = cli_priv;
            int saved_user = VFS::getInstance().get_user_id();
            UserRole saved_role = VFS::getInstance().get_role();
            cli_priv = 0;
            VFS::getInstance().set_user_id(-1);
            VFS::getInstance().set_role(UserRole::R0_ULTIMATE);
            cli_execute_single(remaining, state, cli_priv);
            cli_priv = saved_priv;
            VFS::getInstance().set_user_id(saved_user);
            VFS::getInstance().set_role(saved_role);
            return;
        }
    }

    int back_idx = -1;
    int front_idx = -1;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (back_idx == -1 && is_back_cmd(tokens[i])) back_idx = i;
        if (front_idx == -1 && is_front_cmd(tokens[i])) front_idx = i;
        if (back_idx != -1 && front_idx != -1) break;
    }

    if (front_idx != -1) {
        std::string cmd_name = tokens[front_idx];
        std::vector<std::string> cmd_args;
        for (int i = 0; i < front_idx; ++i) {
            // 🚀 核心修复：剔除引导词，防止 open /text/*.xy del 把 open 当成路径传给 del
            if (tokens[i] == "open" || tokens[i] == "sft" || tokens[i] == "input") {
                continue; 
            }
            cmd_args.push_back(tokens[i]);
        }
        dispatch_command(cmd_name, cmd_args, state, cli_priv);
        return;
    }

    if (back_idx != -1) {
        std::string cmd_name = tokens[back_idx];
        std::vector<std::string> cmd_args;
        for (size_t i = back_idx + 1; i < tokens.size(); ++i) cmd_args.push_back(tokens[i]);
        dispatch_command(cmd_name, cmd_args, state, cli_priv);
        return;
    }

    std::string cmd_name = tokens[0];
    std::vector<std::string> cmd_args(tokens.begin() + 1, tokens.end());
    dispatch_command(cmd_name, cmd_args, state, cli_priv);
}

// ============================================================
// 前台交互模式
// ============================================================
int run_cli_mode(DCLState& state) {
    static thread_local int g_cli_priv = 1;
    std::string line;
    std::string multi_line_cache = "";

    while (true) {
        kernel_process_signals();
        AMM::getInstance().reclaim_expired();

        line.clear();
        while (line.empty()) {
            std::string prompt = "\033[1;35m" + get_prompt(VFS::getInstance().get_cwd()) + "\033[0m";
            if (!multi_line_cache.empty()) {
                prompt = "\033[1;33m... \033[0m";
            }
            std::cout << prompt;
            std::cout.flush();
            DCLKeyboard::set_prompt(prompt);

            KeyInput key = DCLKeyboard::read();
            if (key.event == KeyEvent::TEXT || key.event == KeyEvent::COMMAND) line = key.raw;
            else if (key.event == KeyEvent::SYS_KILL) line = "SYS_KILL"; 
            else if (key.event == KeyEvent::SYS_IO)   line = "SYS_IO";
            else if (key.event == KeyEvent::SYS_OI)   line = "SYS_OI";
            else if (key.event == KeyEvent::SYS_DR)   line = "SYS_DR";
        }
        DCLKeyboard::add_history(line);

        if (!line.empty() && line.back() == '\\') {
            line.pop_back();
            multi_line_cache += line + "\n";
            continue;
        }

        multi_line_cache += line;
        line = multi_line_cache;
        multi_line_cache.clear();

        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        line.erase(0, first);

        if (line.rfind("sudo ", 0) == 0) {
            std::string r3name = drt_get_r3_name();
            g_sudo_pass = read_password_silent(r3name + " SSH:");
        }
        
        cli_execute_single(line, state, g_cli_priv);
    }
    return 0;
}