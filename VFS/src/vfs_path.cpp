#include "../include/vfs.h"
#include "disk_image_driver.h"
#include "disk_fs.h"

#include <algorithm>
#include <iostream>
#include <cctype>
#include <sstream>
#include <vector>

// 🚀 移除了 g_multi_drive_mode 全局变量，完全由底层分区表决定

void Directory::add_child(std::shared_ptr<Node> node) {
    // 🚀 修复：parent 是 weak_ptr，直接赋值即可
    node->parent = shared_from_this();
    children[node->name] = node;
}

VFS& VFS::getInstance() {
    static VFS instance;
    return instance;
}

// ============================================================
// 🚀 核心新增：检查分区表中是否真的存在某个盘符（如 'C'）
// ============================================================
bool VFS::has_drive_letter(char drive) const {
    if (!disk_driver) return false;
    PhysicalLogicalMap pmap{};
    if (!DiskFileSystem::read_partition_table(disk_driver.get(), pmap)) return false;
    
    for (uint32_t i = 0; i < pmap.part_count; ++i) {
        // 判断分区名是否匹配单字母盘符（比如格式化为 "C" 的分区）
        if (std::toupper(pmap.parts[i].part_name[0]) == std::toupper(drive) &&
            pmap.parts[i].part_name[1] == '\0') {
            return true;
        }
    }
    return false;
}

void VFS::init(const std::string& volume_label) {
    std::unique_lock<std::shared_mutex> lock(vfs_mutex); // 🚀 初始化时加写锁

    disk_driver = std::make_shared<DiskImageDriver>();
    if (!disk_driver->open("disk.img")) {
        std::cout << "[VFS] 未找到硬盘镜像，正在创建 16MB 虚拟硬盘...\n";
        disk_driver->create_image("disk.img", 1024); 
        disk_driver->open("disk.img");
        DiskFileSystem::format(disk_driver.get(), volume_label);
    } else {
        DiskFileSystem::rebuild_bitmap(disk_driver.get());
    }

    invisible_root = std::make_shared<Directory>("");
    trunk = std::make_shared<Directory>("/", 0, invisible_root); 
    invisible_root->add_child(trunk);

    current_cwd = "/"; 
}

// ============================================================
// 🚀 【核心加强】set_cwd：绝对原子性，拒绝任何带病路径！
// ============================================================
void VFS::set_cwd(const std::string& new_cwd) {
    std::string norm_path = normalize_path(new_cwd);

    std::unique_lock<std::shared_mutex> lock(vfs_mutex); // 🚀 修改 CWD 必须加写锁

    if (norm_path.empty() || norm_path == "/") {
        current_cwd = "/";
        return;
    }

    // 释放锁，避免 get_node 内部再次加锁死锁
    lock.unlock();
    auto node = get_node(norm_path);
    if (!node || !std::dynamic_pointer_cast<Directory>(node)) {
        return; 
    }

    lock.lock(); // 重新加锁修改
    current_cwd = norm_path;
}

// ============================================================
// 🚀 【核心加强】normalize_path：挂载点感知 + 路径栈绝对压缩
// ============================================================
std::string VFS::normalize_path(const std::string& input) const {
    std::shared_lock<std::shared_mutex> lock(vfs_mutex); // 🚀 读操作加读锁

    if (input.empty()) return current_cwd;
    std::string p = input;

    std::replace(p.begin(), p.end(), '\\', '/');

    // 1. 处理类似 "C:/xxx" 或 "C:xxx" 的输入
    if (p.length() >= 2 && std::isalpha(p[0]) && p[1] == ':') {
        char drive = std::toupper(p[0]);
        std::string remainder = p.substr(2); 
        if (remainder.empty() || remainder[0] != '/') remainder = "/" + remainder;

        // 🚀 核心修复：只有分区表里真的有这个盘，才保留 /C:/ 前缀
        if (has_drive_letter(drive)) {
            p = "/" + std::string(1, drive) + ":" + remainder; 
        } else {
            p = remainder; // 单盘没有该盘符，直接当根目录路径处理
        }
    } 
    else if (p[0] == '/') {
        // 2. 处理绝对路径，如 /C:/xxx
        if (p.length() >= 4 && std::isalpha(p[1]) && p[2] == ':' && p[3] == '/') {
            char drive = std::toupper(p[1]);
            if (!has_drive_letter(drive)) {
                // 输入了 /C:/ 但底层不存在，剥离盘符变成 /xxx
                p = p.substr(3); 
            }
        }
    } 
    else {
        // 3. 相对路径
        if (current_cwd.empty()) {
            p = "/" + p;
        } else if (current_cwd.back() == '/') {
            p = current_cwd + p;
        } else {
            p = current_cwd + "/" + p;
        }
    }

    size_t pos;
    while ((pos = p.find("//")) != std::string::npos) p.replace(pos, 2, "/");

    std::vector<std::string> stack;
    std::stringstream ss(p);
    std::string token;
    
    while (std::getline(ss, token, '/')) {
        if (token.empty() || token == ".") {
            continue;
        } else if (token == "..") {
            if (!stack.empty()) stack.pop_back();
        } else {
            stack.push_back(token);
        }
    }

    p = "/";
    for (size_t i = 0; i < stack.size(); ++i) {
        p += stack[i];
        if (i + 1 < stack.size()) p += "/";
    }
    
    return p.empty() ? "/" : p;
}

// ================== 获取显示路径 ==================
std::string VFS::get_display_path(const std::string& vfs_path) const {
    if (vfs_path.empty()) return "/";

    std::string p = vfs_path;
    
    // 🚀 只有底层确实存在该盘符时，才在 UI 显示时将其转换为 "C:/..."
    if (p.length() >= 4 && p[0] == '/' && std::isalpha(p[1]) && p[2] == ':' && p[3] == '/') {
        char drive = std::toupper(p[1]);
        if (has_drive_letter(drive)) {
            return p.substr(1); // 去掉开头的斜杠
        }
    }
    
    return p; 
}

bool VFS::is_home_directory(const std::string& vfs_path) const {
    // 标准 Linux 风格家目录
    if (vfs_path.find("/home/") == 0 || vfs_path.find("/Users/") == 0) return true;
    
    // 带盘符的家目录 /C:/home/ 或 /C:/Users/
    if (vfs_path.length() >= 4 && vfs_path[0] == '/' && std::isalpha(vfs_path[1]) && vfs_path[2] == ':') {
        if (vfs_path.find("/home/", 3) == 3 || vfs_path.find("/Users/", 3) == 3) {
            return true;
        }
    }
    return false;
}

// ============================================================
// 🚀 路径前缀判断（按路径段，避免 /dev 误匹配 /dev2）
// ============================================================
bool VFS::path_starts_with(const std::string& path, const std::string& prefix) {
    if (prefix.empty()) return false;
    if (path.size() < prefix.size()) return false;
    if (path.compare(0, prefix.size(), prefix) != 0) return false;
    if (path.size() == prefix.size()) return true;
    return path[prefix.size()] == '/';
}

// ============================================================
// 🚀 挂载与卸载（加入线程安全）
// ============================================================
bool VFS::mount_disk(const std::string& mount_point,
                     std::shared_ptr<DiskImageDriver> driver,
                     std::function<std::shared_ptr<Node>(const std::string&)> resolver) {
    std::string norm = normalize_path(mount_point);
    std::unique_lock<std::shared_mutex> lock(vfs_mutex); // 🚀 修改表必须加写锁

    // 挂载点父目录必须存在
    std::string parent = norm.substr(0, norm.find_last_of('/'));
    if (parent.empty()) parent = "/";
    if (parent != "/" && !get_node(parent)) {
        return false;
    }

    MountEntry entry;
    entry.driver   = driver;
    entry.resolver = resolver;
    mount_table[norm] = entry;
    return true;
}

bool VFS::unmount_disk(const std::string& mount_point) {
    std::string norm = normalize_path(mount_point);
    std::unique_lock<std::shared_mutex> lock(vfs_mutex); // 🚀 修改表必须加写锁

    auto it = mount_table.find(norm);
    if (it == mount_table.end()) return false;
    mount_table.erase(it);

    // 🚀 核心修复：如果当前 CWD 就在被卸载的盘里，必须强制踢回根目录！
    if (path_starts_with(current_cwd, norm)) {
        current_cwd = "/";
    }

    return true;
}

bool VFS::find_mount(const std::string& vfs_path,
                     std::string& out_mount_point,
                     MountEntry& out_entry) {
    std::string norm = normalize_path(vfs_path);
    std::shared_lock<std::shared_mutex> lock(vfs_mutex); // 🚀 查表加读锁

    std::string best;
    for (auto& [mp, entry] : mount_table) {
        if (path_starts_with(norm, mp)) {
            if (mp.size() > best.size()) {
                best = mp;
                out_entry = entry;
            }
        }
    }
    if (best.empty()) return false;
    out_mount_point = best;
    return true;
}