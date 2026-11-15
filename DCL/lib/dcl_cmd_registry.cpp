#include "dcl_cmd_registry.h"
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>

CommandRegistry& CommandRegistry::getInstance() {
    static CommandRegistry instance;
    return instance;
}

// ---------- 只扫一次：进程运行期间只执行一次 ----------
void CommandRegistry::init() {
    // 【核心】已扫描过 → 直接返回，零开销
    if (initialized_) return;
    initialized_ = true;

    cmd_map.clear();

    const char* path_env = std::getenv("PATH");
    if (!path_env) return;

    std::stringstream ss(path_env);
    std::string dir_path;

    // 遍历 PATH 里的每个目录
    while (std::getline(ss, dir_path, ':')) {
        DIR* dir = opendir(dir_path.c_str());
        if (!dir) continue;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] == '.') continue;

            // 拼接完整路径
            std::string full_path_str = dir_path;
            if (!full_path_str.empty() && full_path_str.back() != '/') {
                full_path_str += '/';
            }
            full_path_str += entry->d_name;

            // 判断是不是可执行普通文件
            struct stat file_stat;
            if (stat(full_path_str.c_str(), &file_stat) == 0) {
                if (S_ISREG(file_stat.st_mode)) {
                    if (access(full_path_str.c_str(), X_OK) == 0) {
                        // 保留 PATH 中最靠前的一个
                        if (cmd_map.find(entry->d_name) == cmd_map.end()) {
                            cmd_map[entry->d_name] = full_path_str;
                        }
                    }
                }
            }
        }
        closedir(dir);
    }
}

// ---------- 强制重扫（比如用户中途改了 PATH） ----------
void CommandRegistry::refresh() {
    initialized_ = false;
    cmd_map.clear();
    init();
}

std::string CommandRegistry::find(const std::string& cmd_name) const {
    auto it = cmd_map.find(cmd_name);
    if (it != cmd_map.end()) {
        return it->second;
    }
    return "";
}

void CommandRegistry::dump() const {
    std::cout << "=== 已注册命令 (" << cmd_map.size() << " 个) ===\n";
    for (const auto& [name, path] : cmd_map) {
        std::cout << "  " << name << " -> " << path << "\n";
    }
    std::cout << "=========================\n";
}