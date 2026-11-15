#include "../include/dcl_sft.h"
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <chrono>
#include <ctime>
#include "../../VFS/include/vfs.h"

// 🚀 引入获取用户名的外部函数（定义在 dcl_drt.cpp）
extern std::string drt_get_name_by_uid(int uid);

// ============================================================
// 🚀 核心更新：统一路径前缀解析 ( ~/ 家目录, / 干目录, ~ 当前目录, ./ 当前目录 )
// ============================================================
static std::string resolve_path_prefix_sft(const std::string& raw) {
    if (raw.empty()) return raw;

    // 1. 单独一个 "/"，代表干目录，直接返回
    if (raw == "/") return "/";

    std::string cwd = VFS::getInstance().get_cwd();
    if (cwd.empty()) cwd = "/";

    // 2. 单独一个 "~"，代表当前工作目录
    if (raw == "~") return cwd;

    // 3. 单独一个 "."，代表当前工作目录
    if (raw == ".") return cwd;

    // 4. "~/" 开头，代表家目录 /home/用户名
    if (raw.size() >= 2 && raw[0] == '~' && raw[1] == '/') {
        int uid = VFS::getInstance().get_user_id();
        std::string uname = drt_get_name_by_uid(uid);
        std::string home_dir = uname.empty() ? "/" : ("/home/" + uname);
        
        if (home_dir.back() == '/') return home_dir + raw.substr(2);
        return home_dir + raw.substr(1);
    }

    // 5. "./" 开头，代表当前目录下面的路径
    if (raw.size() >= 2 && raw[0] == '.' && raw[1] == '/') {
        if (cwd.back() == '/') return cwd + raw.substr(2);
        return cwd + raw.substr(1);
    }

    // 6. 绝对的 "/" 开头，以及其他相对路径，交给底层的 normalize_path 去处理
    return raw;
}

std::string sft_cmd(DCLState& state, const std::vector<std::string>& args) {
    if (args.empty()) {
        return "用法: \n  sft <名字> [-R|-L]\n  sft <源> \\ <目标>\n  sft <源> | <目标>\n  sft <旧名> / <新名>\n  sft <源> ! (移入回收站)";
    }

    // =========================================================
    // 🚀 操作符解析：优先匹配 \、|、!（这三个不会是合法路径开头）
    //    / 作为操作符时，必须是 args 里唯一的 "/" 参数（即整段等于 "/"）
    // =========================================================
    std::string op = "";
    int op_idx = -1;

    // 第一轮：优先找 \、|、!
    for (int i = 0; i < (int)args.size(); ++i) {
        if (args[i] == "\\" || args[i] == "|" || args[i] == "!") {
            op = args[i];
            op_idx = i;
            break;
        }
    }

    // 第二轮：如果没有 \、|、!，且能找到恰好等于 "/" 的参数，才认为它是重命名操作符
    if (op.empty()) {
        for (int i = 1; i < (int)args.size(); ++i) {
            if (args[i] == "/") {
                op = "/";
                op_idx = i;
                break;
            }
        }
    }

    // =========================================================
    // 🚀 无操作符逻辑：直接创建
    // =========================================================
    if (op.empty()) {
        if (args.size() > 2) {
            return "错误: 参数过多。用法: sft <名字> [-R|-L]";
        }

        // 修改点 1：使用新的解析函数
        std::string name = resolve_path_prefix_sft(args[0]);
        std::string flag = (args.size() == 2) ? args[1] : "";

        if (!flag.empty() && flag != "-R" && flag != "-L") {
            return "错误: 未知参数 (" + flag + ")。用法: sft <名字> [-R|-L]";
        }
        if (name.empty()) return "错误: 请输入文件或目录名";

        bool is_dir = false;
        if (flag == "-R") {
            is_dir = true;
        } else if (flag == "-L") {
            is_dir = false;
        } else {
            size_t dot_pos = name.find_last_of('.');
            size_t slash_pos = name.find_last_of('/');
            if (dot_pos != std::string::npos && (slash_pos == std::string::npos || dot_pos > slash_pos)) {
                is_dir = false;
            } else {
                is_dir = true;
            }
        }

        std::string vfs_name = VFS::getInstance().normalize_path(name);

        if (VFS::getInstance().get_node(vfs_name)) {
            return "错误: 目标已存在";
        }

        std::string result;
        if (is_dir) {
            result = VFS::getInstance().create_directory(vfs_name);
        } else {
            result = VFS::getInstance().create_file(vfs_name);
        }

        if (result.empty()) return "";
        return result;
    }

    // =========================================================
    // 🚀 有操作符逻辑：移动、复制、重命名
    // =========================================================
    if (op_idx == -1 || op_idx - 1 < 0) {
        return "错误: 操作符位置不正确，请检查用法。";
    }

    // 支持多空格路径：把操作符左边所有参数拼起来作为源
    std::string src_name;
    for (int i = 0; i < op_idx; ++i) {
        if (!src_name.empty()) src_name += " ";
        src_name += args[i];
    }

    // 修改点 2：使用新的解析函数
    src_name = resolve_path_prefix_sft(src_name);
    std::string vfs_src = VFS::getInstance().normalize_path(src_name);

    if (!VFS::getInstance().get_node(vfs_src)) {
        return "错误: 找不到源文件/目录 (" + src_name + ")";
    }

    // 🚀 拦截：不能把回收站根目录或盘符根目录扔进去
    if (vfs_src == "/" || vfs_src == "/ast" || vfs_src == "/bin" || vfs_src == "/xyby") {
        return "错误: 禁止将系统核心目录移入回收站";
    }

    std::string vfs_dst = "";

    // =========================================================
    // 🚀 特殊操作符 "!"：移入当前用户的专属回收站
    // =========================================================
    if (op == "!") {
        // 1. 获取当前用户
        int uid = VFS::getInstance().get_user_id();
        std::string uname = drt_get_name_by_uid(uid);
        if (uname.empty()) uname = "XY"; // 兜底
        
        std::string trash_dir = "/ast/" + uname;
        
        // 2. 确保回收站目录存在
        if (!VFS::getInstance().get_node(trash_dir)) {
            VFS::getInstance().create_directory(trash_dir);
        }

        // 3. 获取源文件名
        size_t last_slash = vfs_src.find_last_of('/');
        std::string base_name = (last_slash == std::string::npos) ? vfs_src : vfs_src.substr(last_slash + 1);

        vfs_dst = trash_dir + "/" + base_name;

        // 4. 🚀 重名冲突处理：如果回收站里已经有这个文件，加上时间戳
        if (VFS::getInstance().get_node(vfs_dst)) {
            auto now = std::chrono::system_clock::now();
            auto t = std::chrono::system_clock::to_time_t(now);
            std::string ts = std::to_string(t);
            vfs_dst = trash_dir + "/" + base_name + "_" + ts;
        }
    }
    // =========================================================
    // 🚀 常规操作符：\、|、/
    // =========================================================
    else {
        if (op_idx + 1 >= (int)args.size()) {
            return "错误: 缺少目标路径。";
        }

        std::string dst_name;
        for (int i = op_idx + 1; i < (int)args.size(); ++i) {
            if (!dst_name.empty()) dst_name += " ";
            dst_name += args[i];
        }

        // 修改点 3：使用新的解析函数
        dst_name = resolve_path_prefix_sft(dst_name);
        vfs_dst = VFS::getInstance().normalize_path(dst_name);

        // 🚀 智能目标路径处理：目标是已存在的目录时，拼接源文件 basename
        auto dst_node = VFS::getInstance().get_node(vfs_dst);
        if (dst_node) {
            if (std::dynamic_pointer_cast<Directory>(dst_node) != nullptr) {
                size_t last_slash = vfs_src.find_last_of('/');
                std::string base_name = (last_slash == std::string::npos) ? vfs_src : vfs_src.substr(last_slash + 1);

                if (!vfs_dst.empty() && vfs_dst.back() == '/') {
                    vfs_dst += base_name;
                } else {
                    vfs_dst += "/" + base_name;
                }

                if (VFS::getInstance().get_node(vfs_dst)) {
                    return "错误: 目标已存在 (" + vfs_dst + ")";
                }
            } else {
                // 目标是文件：重命名时禁止覆盖
                if (op == "/") {
                    return "错误: 重命名目标已存在 (" + vfs_dst + ")";
                }
            }
        }
    }

    // =========================================================
    // 🚀 执行 VFS 操作
    // =========================================================
    std::string result;
    if (op == "\\" || op == "!") {
        result = VFS::getInstance().move_node(vfs_src, vfs_dst);
    } else if (op == "|") {
        result = VFS::getInstance().copy_node(vfs_src, vfs_dst);
    } else if (op == "/") {
        result = VFS::getInstance().rename_node(vfs_src, vfs_dst);
    }

    if (result.empty()) {
        return "";  // 静默成功
    }
    return result;
}