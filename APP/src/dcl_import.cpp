#include "../include/dcl_import.h"
#include "../../VFS/include/vfs.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <algorithm>

extern std::string drt_get_name_by_uid(int uid);

// 🚀 统一路径前缀解析
static std::string resolve_vfs_path_prefix(const std::string& raw) {
    if (raw.empty()) return raw;
    if (raw == "/") return "/";
    
    std::string cwd = VFS::getInstance().get_cwd();
    if (cwd.empty()) cwd = "/";

    if (raw == "~" || raw == ".") return cwd;

    if (raw.size() >= 2 && raw[0] == '~' && raw[1] == '/') {
        int uid = VFS::getInstance().get_user_id();
        std::string uname = drt_get_name_by_uid(uid);
        std::string home_dir = uname.empty() ? "/" : ("/home/" + uname);
        if (home_dir.back() == '/') return home_dir + raw.substr(2);
        return home_dir + raw.substr(1);
    }

    if (raw.size() >= 2 && raw[0] == '.' && raw[1] == '/') {
        if (cwd.back() == '/') return cwd + raw.substr(2);
        return cwd + raw.substr(1);
    }

    return raw;
}

// 🚀 递归确保 VFS 目录存在（带并发容错）
static bool vfs_ensure_dir(const std::string& vfs_path) {
    std::string norm = VFS::getInstance().normalize_path(vfs_path);
    if (norm.empty() || norm == "/") return true;
    std::stringstream ss(norm);
    std::string seg, cur = "";
    while (std::getline(ss, seg, '/')) {
        if (seg.empty()) continue;
        cur += "/" + seg;
        
        if (!VFS::getInstance().get_node(cur)) {
            std::string err = VFS::getInstance().create_directory(cur);
            if (!err.empty()) {
                // 🚀 核心修复 1：如果创建失败，可能因为并发冲突（别人刚建好）。
                // 必须重新查一次，只要现在存在了，就继续往下走。如果不存在，才真正返回失败。
                if (!VFS::getInstance().get_node(cur)) {
                    std::cerr << "[IMPORT ERROR] 创建目录失败: " << cur << " (" << err << ")\n";
                    return false; 
                }
            }
        }
    }
    return true;
}

// 🚀 强制赋予私有资源属性
static void force_private_resource(const std::string& vfs_path) {
    if (vfs_path.find("/home/") == 0) {
        ResourceInfo ri;
        ri.type     = ResourceType::PRIVATE;
        ri.owner_id = VFS::getInstance().get_user_id();
        ri.group_id = -1;
        ri.perms    = PERM_ALL;
        VFS::getInstance().set_resource_info(vfs_path, ri);
    }
}

std::string import_cmd(DCLState& state, const std::vector<std::string>& args) {
    if (args.empty()) {
        return "用法: import <宿主机路径> [VFS目标目录]\n"
               "示例: import ~/音乐\n"
               "示例: import /storage/emulated/0/Download/test.mp3 /home/XY/下载";
    }

    std::string host_src = args[0];
    std::string vfs_dest_dir;

    if (args.size() >= 2) {
        vfs_dest_dir = resolve_vfs_path_prefix(args[1]);
    } else {
        int uid = VFS::getInstance().get_user_id();
        std::string uname = drt_get_name_by_uid(uid);
        if (uname.empty()) uname = "XY";
        vfs_dest_dir = "/home/" + uname;
    }

    // 1. 解析物理机真实路径
    std::string real_host_src = host_src;
    if (!real_host_src.empty() && real_host_src[0] == '~') {
        const char* home = getenv("HOME");
        if (home) real_host_src = std::string(home) + real_host_src.substr(1);
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path src_path(real_host_src);

    if (!fs::exists(src_path, ec)) {
        return "错误: 宿主机上找不到该路径: " + real_host_src;
    }

    // 2. 确保 VFS 目标根目录存在（如果失败，直接拦截退出）
    if (!vfs_ensure_dir(vfs_dest_dir)) {
        return "错误: 无法创建 VFS 目标目录 " + vfs_dest_dir;
    }
    force_private_resource(vfs_dest_dir);

    int file_count = 0;
    int skip_count = 0;

    // 3. 导入单文件
    if (fs::is_regular_file(src_path, ec)) {
        std::string filename = src_path.filename().string();
        
        // 🚀 防御：拦截超长文件名（通常 VFS 节点名超过 255 字节就会崩溃或静默失败）
        if (filename.length() > 200) {
            return "错误: 文件名过长 (" + std::to_string(filename.length()) + " 字节)，VFS 无法处理。请缩短文件名。";
        }

        std::string vfs_file_path = vfs_dest_dir;
        if (vfs_file_path.back() == '/') {
            vfs_file_path += filename;
        } else if (!fs::path(vfs_dest_dir).has_extension()) {
             vfs_file_path += "/" + filename;
        }

        std::ifstream ifs(real_host_src, std::ios::binary);
        if (!ifs.is_open()) return "无法读取物理文件: " + real_host_src;
        
        std::stringstream buf;
        buf << ifs.rdbuf();
        std::string content = buf.str();

        auto existing = VFS::getInstance().get_node(vfs_file_path);
        std::string write_err;
        if (existing) {
            write_err = VFS::getInstance().write_file(vfs_file_path, content);
        } else {
            write_err = VFS::getInstance().create_file(vfs_file_path, content);
        }
        
        if (!write_err.empty()) {
            return "[-] VFS 底层写入失败: " + write_err;
        }
        
        force_private_resource(vfs_file_path);
        return "已成功导入文件到: " + vfs_file_path;
    } 
    // 4. 导入文件夹
    else if (fs::is_directory(src_path, ec)) {
        std::string last_err = "";
        for (const auto& entry : fs::recursive_directory_iterator(src_path, ec)) {
            if (entry.is_regular_file()) {
                std::string path_str = entry.path().string();
                if (path_str.find("/.git/") != std::string::npos || path_str.find("/node_modules/") != std::string::npos) {
                    continue; 
                }

                fs::path rel_path = fs::relative(entry.path(), src_path);
                std::string vfs_file_path = vfs_dest_dir;
                std::string sub_path = rel_path.string();
                std::replace(sub_path.begin(), sub_path.end(), '\\', '/');
                
                vfs_file_path += "/" + sub_path;

                size_t slash = vfs_file_path.find_last_of('/');
                if (slash != std::string::npos) {
                    // 🚀 核心修复 2：必须检查目录创建是否成功，失败直接跳过该文件！
                    if (!vfs_ensure_dir(vfs_file_path.substr(0, slash))) {
                        last_err = "无法创建父目录: " + vfs_file_path.substr(0, slash);
                        skip_count++;
                        continue; // 🚀 直接跳过，绝对不往下传错误路径
                    }
                    force_private_resource(vfs_file_path.substr(0, slash));
                }

                std::ifstream ifs(entry.path(), std::ios::binary);
                if (ifs.is_open()) {
                    std::stringstream buf;
                    buf << ifs.rdbuf();
                    std::string content = buf.str();

                    auto existing = VFS::getInstance().get_node(vfs_file_path);
                    std::string write_err;
                    if (existing) {
                        write_err = VFS::getInstance().write_file(vfs_file_path, content);
                    } else {
                        write_err = VFS::getInstance().create_file(vfs_file_path, content);
                    }
                    
                    if (!write_err.empty()) {
                        last_err = write_err;
                        skip_count++;
                    } else {
                        force_private_resource(vfs_file_path);
                        file_count++;
                    }
                } else {
                    skip_count++;
                }
            }
        }
        
        std::string msg = "已成功导入文件夹，共 " + std::to_string(file_count) + " 个文件到 " + vfs_dest_dir;
        if (skip_count > 0) {
            msg += " (跳过 " + std::to_string(skip_count) + " 个文件)";
            if (!last_err.empty()) {
                msg += "\n[-] 最后一次错误: " + last_err;
            }
        }
        return msg;
    }

    return "错误: 目标既不是普通文件也不是目录";
}