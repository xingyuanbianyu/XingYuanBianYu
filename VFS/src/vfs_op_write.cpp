// vfs_op_write.cpp
#include "../include/vfs.h"
#include "disk_image_driver.h" 
#include "disk_fs.h"           
#include <iostream>
#include <shared_mutex>

// ============================================================
// 🚀 通用辅助：判断路径是否在挂载点内，返回用的 driver 和从盘子路径
// ============================================================
struct MountResolve {
    BlockDevice* disk = nullptr;
    std::string  target_path;   // 在对应盘上的路径
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

// ============================================================
// 🚀 覆盖写入
// ============================================================
std::string VFS::write_file(const std::string& vfs_path, const std::string& content) {
    std::string norm_path = normalize_path(vfs_path);

    // 🚀 权限校验
    if (!check_resource_access(norm_path)) {
        return "错误: 权限不足，无法写入该文件";
    }

    // 🚀 受保护路径
    if (is_protected_path(norm_path)) {
        if (current_role != UserRole::R3_SYS_ADMIN) {
            return "错误: 该文件为系统账户配置，只读（需要 R3 权限）";
        }
    }

    // 🚀 目标不能是目录
    auto node = get_node(norm_path);
    if (node && std::dynamic_pointer_cast<Directory>(node)) {
        return "错误: 目标是一个目录，无法写入";
    }

    // 🚀 解析挂载点
    MountResolve mr = resolve_mount(norm_path);
    if (!mr.disk) {
        return "错误: 无法定位存储设备";
    }

    // 🚀 目标不存在时，先校验父目录 + 创建空文件
    DirEntry entry;
    if (!DiskFileSystem::find_entry(mr.disk, mr.target_path, entry)) {
        std::string parent_path = DiskFileSystem::get_parent_path(mr.target_path);

        // 父目录校验（根目录特判）
        if (parent_path != "/" && !parent_path.empty()) {
            DirEntry parent_entry;
            if (!DiskFileSystem::find_entry(mr.disk, parent_path, parent_entry)) {
                return "错误: 父目录不存在，无法创建文件";
            }
            if (parent_entry.type != 1) {
                return "错误: 父路径不是一个目录";
            }
        }

        // 用正确的 driver 创建文件
        if (!DiskFileSystem::create_file(mr.disk, mr.target_path)) {
            return "错误: 创建文件失败";
        }

        // 同步 owner_id
        DirEntry new_entry;
        if (DiskFileSystem::find_entry(mr.disk, mr.target_path, new_entry)) {
            InodeEntry ie;
            if (DiskFileSystem::inode_read(mr.disk, new_entry.inode_id, ie)) {
                ie.owner_id = static_cast<uint32_t>(current_user_id);
                DiskFileSystem::inode_write(mr.disk, new_entry.inode_id, ie);
            }
        }
    }

    // 🚀 用正确的 driver 写入内容（底层有 COW 保护）
    if (!DiskFileSystem::write_file(mr.disk, mr.target_path, content)) {
        return "错误: 写入磁盘失败";
    }

    // ============================================================
    // 🚀 核心修复：写入成功后，同步更新内存中 Node 的 size 和内容
    //    🚀 必须加写锁！防止并发读取时 string 重新分配导致段错误。
    // ============================================================
    auto updated_node = get_node(norm_path);
    if (updated_node) {
        std::unique_lock<std::shared_mutex> lock(vfs_mutex); // 🚀 加锁保护
        updated_node->size = content.size(); 
        
        auto file_node = std::dynamic_pointer_cast<File>(updated_node);
        if (file_node) {
            file_node->content = content;
        }
    }

    return "";
}

// ============================================================
// 🚀 追加写入
// ============================================================
std::string VFS::append_file(const std::string& vfs_path, const std::string& content) {
    std::string norm_path = normalize_path(vfs_path);

    // 🚀 权限校验
    if (!check_resource_access(norm_path)) {
        return "错误: 权限不足，无法追加该文件";
    }

    // 🚀 目标类型检查
    std::string old_content;
    auto node = get_node(norm_path);

    if (node) {
        if (std::dynamic_pointer_cast<Directory>(node)) {
            return "错误: 目标是一个目录，无法追加";
        }
        
        // 🚀 优化：如果内存里已经有 content，直接用它，避免重复读盘
        auto file_node = std::dynamic_pointer_cast<File>(node);
        if (file_node && !file_node->content.empty()) {
            old_content = file_node->content;
        } else {
            // 如果内存无缓存，则从磁盘读
            old_content = read_file(norm_path);
        }
    } else {
        old_content = "";
    }

    // 🚀 防 OOM 核心修复：追加操作底层依赖全量内存拷贝
    // 限制追加操作的最大合并体积（例如 16MB），防止移动端直接闪退
    const size_t MAX_APPEND_SIZE = 16 * 1024 * 1024; 
    if (old_content.size() + content.size() > MAX_APPEND_SIZE) {
        return "错误: 追加后文件过大，暂不支持超大文件的连续追加写入";
    }

    // 🚀 拼接后直接调用 write_file
    return write_file(norm_path, old_content + content);
}