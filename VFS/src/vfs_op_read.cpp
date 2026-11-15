#include "../include/vfs.h"
#include "disk_image_driver.h"
#include "disk_fs.h"
#include <algorithm>
#include <iostream>
#include <shared_mutex>

// 🚀 去掉 ANSI 色码
static std::string strip_color(const std::string& s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
            size_t j = i + 2;
            while (j < s.size() && s[j] != 'm') j++;
            if (j < s.size()) j++;
            i = j;
        } else {
            out += s[i++];
        }
    }
    return out;
}

// 🚀 剥前缀（兼容 [目录] / [文件] / [挂载]）
static std::string strip_prefix(const std::string& s) {
    size_t sp = s.find(' ');
    if (sp == std::string::npos) return s;
    return s.substr(sp + 1);
}

// 🚀 统一获取干净名字
static std::string get_clean_name(const std::string& raw) {
    return strip_prefix(strip_color(raw));
}

// ============================================================
// 🚀 通用辅助：判断路径是否在挂载点内，返回用的 driver 和从盘子路径
// ============================================================
struct MountResolve {
    BlockDevice* disk = nullptr;
    std::string  target_path;
    bool         in_mount = false;
};

static MountResolve resolve_mount(const std::string& norm) {
    MountResolve r;
    std::string mp;
    VFS::MountEntry entry;

    if (VFS::getInstance().find_mount(norm, mp, entry) && entry.driver) {
        r.disk = entry.driver.get();
        r.target_path = norm.substr(mp.size());
        if (r.target_path.empty()) r.target_path = "/";
        r.in_mount = true;
        return r;
    }

    r.disk = VFS::getInstance().get_disk_driver();
    r.target_path = norm;
    r.in_mount = false;
    return r;
}

// ================== 获取节点 ==================
std::shared_ptr<Node> VFS::get_node(const std::string& vfs_path) {
    std::string norm = normalize_path(vfs_path);

    // 1. 挂载点重定向
    {
        std::string mp;
        MountEntry entry;
        if (find_mount(norm, mp, entry)) {
            std::string sub = norm.substr(mp.size());
            if (sub.empty()) sub = "/";
            if (entry.resolver) {
                return entry.resolver(sub);
            }
        }
    }

    // 2. 根目录特判
    if (norm == "/") {
        return trunk;
    }

    // 3. 查底层 inode（主盘）
    BlockDevice* disk = disk_driver.get();
    if (!disk) return nullptr;

    DirEntry entry;
    if (!DiskFileSystem::find_entry(disk, norm, entry)) {
        return nullptr;
    }

    InodeEntry ie;
    if (!DiskFileSystem::inode_read(disk, entry.inode_id, ie)) {
        return nullptr;
    }

    std::shared_ptr<Node> node;
    if (entry.type == 1) {
        node = std::make_shared<Directory>(entry.name, ie.start_block);
    } else {
        node = std::make_shared<File>(entry.name);
    }

    // 🚀 核心修复：必须填充底层真实元数据，否则上层更新 size 或属性会丢失！
    node->inode_id  = entry.inode_id;
    node->domain_id = ie.domain_id;
    node->size      = ie.size;
    node->is_dir    = (entry.type == 1);

    return node;
}

// ================== 读取文件 ==================
std::string VFS::read_file(const std::string& vfs_path) {
    std::string norm = normalize_path(vfs_path);

    if (norm.empty() || norm == "/") {
        return "错误: 无法读取根目录内容";
    }

    // 🚀 权限校验
    if (!check_resource_access(norm)) {
        return "错误: 权限不足，无法读取该文件";
    }

    // 🚀 类型校验：必须是文件
    auto node = get_node(norm);
    if (!node) {
        return "错误: 文件不存在";
    }
    if (node->is_dir) {
        return "错误: 目标是一个目录，无法作为文件读取";
    }

    // 🚀 OOM 核心防御：大于 32MB 的文件直接拒绝全量载入内存
    const uint64_t MAX_READ_SIZE = 32ULL * 1024 * 1024; 
    if (node->size > MAX_READ_SIZE) {
        return "错误: 文件过大 (>32MB)，禁止使用常规读取（容易导致系统内存溢出 OOM）";
    }

    // 🚀 统一解析挂载点，用正确的 driver 读取
    MountResolve mr = resolve_mount(norm);
    if (!mr.disk) {
        return "错误: 无法定位存储设备";
    }

    return DiskFileSystem::read_file_content(mr.disk, mr.target_path);
}

// ================== 列出目录 ==================
std::vector<std::string> VFS::list_directory(const std::string& vfs_path) {
    std::vector<std::string> result;

    std::string norm = normalize_path(vfs_path);

    // 🚀 权限校验
    if (!check_resource_access(norm)) {
        return {};
    }

    // 🚀 类型校验：必须是目录
    auto node = get_node(norm);
    if (!node || !node->is_dir) {
        return {};
    }

    // 🚀 统一解析挂载点，用正确的 driver 列目录
    MountResolve mr = resolve_mount(norm);
    if (!mr.disk) {
        return {};
    }

    std::vector<std::string> raw = DiskFileSystem::list_dir(mr.disk, mr.target_path);
    for (auto& r : raw) {
        std::string clean = get_clean_name(r);
        
        // 🚀 终极防线 1：VFS 层直接拦截空名字和空转义码，防止终端渲染出空行！
        if (clean.empty()) continue;
        
        if      (r.rfind("[目录] ", 0) == 0) result.push_back("\033[34m" + clean + "\033[0m");
        else if (r.rfind("[文件] ", 0) == 0) result.push_back("\033[37m" + clean + "\033[0m");
        else                                  result.push_back(r);
    }

    // ============================================================
    // 🚀 动态合成挂载点（只在主盘根目录的父级上合成）
    // ============================================================
    if (!mr.in_mount) {
        // 🚀 核心修复：遍历 mount_table 必须加读锁，防止别的终端 mount/unmount 导致红黑树重排崩溃！
        std::shared_lock<std::shared_mutex> lock(vfs_mutex); 
        
        for (auto& [mp, entry] : mount_table) {
            size_t last_slash = mp.find_last_of('/');
            if (last_slash == std::string::npos) continue;
            std::string parent = mp.substr(0, last_slash);
            if (parent.empty()) parent = "/";
            if (parent != norm) continue;

            std::string name = mp.substr(last_slash + 1);
            
            // 🚀 终极防线 2：挂载点名字为空，直接跳过
            if (name.empty()) continue;

            bool exists = false;
            for (auto& r : result) {
                if (get_clean_name(r) == name) { exists = true; break; }
            }
            if (!exists) {
                result.push_back("\033[31m" + name + "\033[0m"); // 挂载点用红色
            }
        }
    }

    // ============================================================
    // 🚀 最终排序（按字母序）
    // ============================================================
    std::sort(result.begin(), result.end(), [](const std::string& a, const std::string& b) {
        std::string ca = get_clean_name(a);
        std::string cb = get_clean_name(b);
        return ca < cb;
    });

    // 🚀 终极防线 3：最后把整个数组扫一遍，如果漏网了带颜色代码的空串，直接拔掉
    result.erase(
        std::remove_if(result.begin(), result.end(), [](const std::string& s) {
            std::string c = get_clean_name(s);
            return c.empty();
        }),
        result.end()
    );

    return result;
}