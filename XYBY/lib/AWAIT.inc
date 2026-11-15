// ============================================================
// AWAIT.inc —— await 关键字处理器（阻塞/非阻塞双模式）
// ============================================================
#include "../../Kernel/Scheduler.h"

bool handleAwait(const std::string& line, MemorySpace& mem) {
    if (line.find("KID:AWAIT") == std::string::npos) return false;

    // 🚀 1. 提取函数名
    size_t name_pos = line.find("NAME:SID:22[");
    if (name_pos == std::string::npos) return true;
    size_t name_end = line.find("]22", name_pos + 12);
    if (name_end == std::string::npos) return true;
    std::string func_name = line.substr(name_pos + 12, name_end - name_pos - 12);

    // 🚀 2. 提取左值变量名
    std::string target_var = "";
    size_t args_pos = line.find("ARGS:");
    size_t vid_pos = line.find("VID:");
    
    if (vid_pos != std::string::npos && (args_pos == std::string::npos || vid_pos < args_pos)) {
        size_t vid_end = line.find(";", vid_pos);
        if (vid_end == std::string::npos) vid_end = line.find("}", vid_pos);
        if (vid_end == std::string::npos) vid_end = line.find(",", vid_pos);
        if (vid_end != std::string::npos && vid_end > vid_pos) {
            target_var = line.substr(vid_pos + 4, vid_end - vid_pos - 4);
        }
    }

    // 🚀 3. 提取参数
    std::string args_str = "";
    if (args_pos != std::string::npos) {
        size_t args_end = line.find("}", args_pos);
        if (args_end == std::string::npos) args_end = line.length();
        args_str = line.substr(args_pos + 5, args_end - args_pos - 5);
    }

    // ============================================================
    // 🚀 4. 【SLEEP 标记】匹配带 e 的 SLEEP:TRUE
    // ============================================================
    if (line.find("SLEEP:TRUE") != std::string::npos) {
        // 第一步：先查表（不存在就报错）
        auto fit = m_functions.find(func_name);
        if (fit == m_functions.end()) {
            std::cerr << "[错误] 函数不存在: " << func_name << std::endl;
            mem.last_return_value = "None";
            return true; 
        }

        // 第二步：函数存在，执行 handleCall（执行你 .xy 里的函数体）
        g_last_xy_return_value = "";
        std::string fake_call_line = "FID:CALL NAME:SID:22[" + func_name + "]22 ARGS:" + args_str;
        handleCall(fake_call_line, mem);

        // 第三步：解析所有参数类型，提取真实毫秒数（支持变量！）
        int ms = 0;
        std::vector<std::string> parsed_args;
        if (!args_str.empty()) {
            std::stringstream ss(args_str);
            std::string t;
            while (std::getline(ss, t, ',')) {
                t = trim(t);
                if (t.empty()) continue;
                if (t.rfind("NID:", 0) == 0) parsed_args.push_back(t.substr(4));
                else if (t.rfind("FLT:", 0) == 0) parsed_args.push_back(t.substr(4));
                else if (t.rfind("VID:", 0) == 0) {
                    std::string vname = t.substr(4);
                    auto it = mem.primary_memory.find(vname);
                    if (it != mem.primary_memory.end()) parsed_args.push_back(it->second.value);
                    else parsed_args.push_back(vname);
                } else if (t.rfind("SID:22[", 0) == 0) {
                    size_t rb = t.find("]22");
                    if (rb != std::string::npos) parsed_args.push_back(t.substr(7, rb - 7));
                } else {
                    parsed_args.push_back(t);
                }
            }
        }

        // args_str 是 "SID:22[await]22,VID:sleep,SID:22[true]22" 
        // 解析后 parsed_args 是 ["await", "1000", "true"]，索引1就是毫秒数
        if (parsed_args.size() >= 2) {
            try { ms = std::stoi(parsed_args[1]); } catch (...) { ms = 0; }
        }

        // 第四步：C++ 底层直接睡！
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));

        // 第五步：写回变量
        if (!target_var.empty()) {
            mem.primary_memory[target_var].value = "None";
        }
        mem.last_return_value = "None";
        
        // 🚀 立刻返回，绝不掉进下面的异步分支
        return true; 
    }

    // ============================================================
    // 🚀 5. 原有的 AWAIT 分流逻辑（其他函数调用走这里）
    // ============================================================
    bool is_blocking = (line.find("AWAIT:TRUE") != std::string::npos);
    std::string ret = "";

    if (is_blocking) {
        g_last_xy_return_value = ""; 
        std::string fake_call_line = "FID:CALL NAME:SID:22[" + func_name + "]22 ARGS:" + args_str;
        handleCall(fake_call_line, mem); 
        
        ret = g_last_xy_return_value;
        if (ret.empty()) ret = "None"; 
        
        if (!target_var.empty()) {
            mem.primary_memory[target_var].value = ret;
        }
    } 
    else {
        if (!target_var.empty()) {
            g_await_target_var = target_var;
        } else {
            g_await_target_var = ""; 
        }

        std::vector<std::string> args;
        if (!args_str.empty()) {
            std::stringstream ss(args_str);
            std::string t;
            while (std::getline(ss, t, ',')) {
                t = trim(t);
                if (t.empty()) continue;
                if (t.rfind("NID:", 0) == 0) args.push_back(t.substr(4));
                else if (t.rfind("FLT:", 0) == 0) args.push_back(t.substr(4));
                else if (t.rfind("VID:", 0) == 0) {
                    std::string vname = t.substr(4);
                    auto it = mem.primary_memory.find(vname);
                    if (it != mem.primary_memory.end()) args.push_back(it->second.value);
                    else args.push_back(vname);
                } else if (t.rfind("SID:22[", 0) == 0) {
                    size_t rb = t.find("]22");
                    if (rb != std::string::npos) args.push_back(t.substr(7, rb - 7));
                } else {
                    args.push_back(t);
                }
            }
        }

        int handle = Kernel::sys_spawn_thread(func_name, args);
        ret = Kernel::sys_await(handle);

        if (!target_var.empty()) {
            mem.primary_memory[target_var].value = ret;
        }
    }

    mem.last_return_value = ret;
    g_await_target_var = ""; 

    return true;
}