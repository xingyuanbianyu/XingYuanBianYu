#include "vfs.h"
#include "../../DD/include/disk_image_driver.h"
#include "../../DD/include/disk_fs.h"
#include <algorithm>
#include <string>
#include <mutex>
#include <shared_mutex>
#include <set>

// ================= 资源系统实现 =================

// ============================================================
// 🚀 get_resource_type：内存注册表绝对优先！
// ============================================================
ResourceType VFS::get_resource_type(const std::string& vfs_path) const {
    std::string path_str = normalize_path(vfs_path);

    // 🚀 核心修复 1：直接查内存注册表，命中就立刻返回，绝不走底层！
    {
        std::shared_lock<std::shared_mutex> lock(vfs_mutex);
        auto it = resource_registry.find(path_str);
        if (it != resource_registry.end()) {
            return it->second.type;
        }
    }

    // 🚀 核心修复 2：如果注册表没有，再查底层磁盘标签
    BlockDevice* dev = get_disk_driver();
    if (dev) {
        std::string tag = DiskFileSystem::get_resource_tag(dev, path_str);

        if (tag == RES_TAG_R0)    return ResourceType::SYSTEM;
        if (tag == RES_TAG_HOME)  return ResourceType::PUBLIC;
        if (tag == RES_TAG_ROOT)  return ResourceType::PRIVATE;
        if (tag == RES_TAG_CMD)   return ResourceType::SPECIAL;
        if (tag == RES_TAG_PATH)  return ResourceType::SYSTEM;
    }

    // 🚀 兜底 1：家目录判定
    if (is_home_directory(path_str)) {
        return ResourceType::PRIVATE;
    }

    // 🚀 兜底 2：用户注册表的前缀匹配（保留你原本的逻辑，但加了锁）
    {
        std::shared_lock<std::shared_mutex> lock(vfs_mutex);
        for (const auto& [reg_path, info] : resource_registry) {
            if (path_str == reg_path || path_str.find(reg_path + "/") == 0) {
                return info.type;
            }
        }
    }

    // 🚀 兜底 3：系统路径
    if (path_str.find("/System") == 0 || path_str.find("/Windows") == 0) {
        return ResourceType::SYSTEM;
    }

    // 🚀 兜底 4：隐藏文件
    size_t last_slash = path_str.find_last_of('/');
    std::string filename = (last_slash == std::string::npos) ? path_str : path_str.substr(last_slash + 1);
    if (!filename.empty() && filename[0] == '.') {
        return ResourceType::SPECIAL;
    }

    return ResourceType::PUBLIC;
}

// ============================================================
// 🚀 set_resource_info：内存 + 底层双写！
// ============================================================
void VFS::set_resource_info(const std::string& vfs_path, const ResourceInfo& info) {
    std::string norm = normalize_path(vfs_path);
    
    // 1. 写入内存注册表
    {
        std::unique_lock<std::shared_mutex> lock(vfs_mutex);
        resource_registry[norm] = info;
    }

    // 2. 🚀 核心修复 3：同步修改底层磁盘 Inode 的 resource_tag
    BlockDevice* dev = get_disk_driver();
    if (dev) {
        std::string tag = RES_TAG_PATH;
        switch (info.type) {
            case ResourceType::SYSTEM:  tag = RES_TAG_PATH; break; 
            case ResourceType::PUBLIC:  tag = RES_TAG_HOME; break;
            case ResourceType::PRIVATE: tag = RES_TAG_ROOT; break;
            case ResourceType::SPECIAL: tag = RES_TAG_CMD;  break;
            default:                    tag = RES_TAG_PATH; break;
        }
        // 写入底层
        DiskFileSystem::set_resource_tag(dev, norm, tag);
    }
}

// ============================================================
// 🚀 get_default_perms：根据资源类型 + 当前用户身份，计算默认权限
// ============================================================
uint32_t VFS::get_default_perms(ResourceType type, const std::string& vfs_path) const {
    std::string norm = normalize_path(vfs_path);

    switch (type) {
        case ResourceType::PUBLIC:
            return PERM_READ | PERM_WRITE | PERM_MOD | PERM_DEL
                 | PERM_CLEAR | PERM_LIST | PERM_EXEC;

        case ResourceType::SYSTEM:
            return PERM_READ | PERM_LIST | PERM_EXEC;

        case ResourceType::PRIVATE: {
            bool is_owner = false;
            // 🚀 判定是否属于当前用户 (按 UID 做目录隔离)
            std::string user_prefix = "/home/" + std::to_string(current_user_id);
            if (norm == user_prefix || norm.rfind(user_prefix + "/", 0) == 0) {
                is_owner = true;
            }
            // R3 管理员拥有私有资源的完整维护权限
            if (current_role == UserRole::R3_SYS_ADMIN) {
                is_owner = true;
            }

            if (is_owner) {
                return PERM_MOD | PERM_DEL | PERM_READ
                     | PERM_LIST | PERM_EXEC | PERM_WRITE;
            } else {
                return PERM_READ | PERM_LIST; // 别人的私有资源只读
            }
        }

        case ResourceType::SPECIAL:
            return PERM_READ | PERM_LIST;

        default:
            return PERM_READ | PERM_LIST;
    }
}

// ============================================================
// 🚀 get_resource_info_dynamic：动态版资源元数据读取
// ============================================================
ResourceInfo VFS::get_resource_info_dynamic(const std::string& vfs_path) const {
    ResourceInfo info;
    std::string norm = normalize_path(vfs_path);

    // 1. 先查注册表（加读锁保护）
    {
        std::shared_lock<std::shared_mutex> lock(vfs_mutex);
        auto it = resource_registry.find(norm);
        if (it != resource_registry.end()) {
            return it->second; // 注册表优先级最高，直接返回完整记录
        }
    }

    // 2. 从磁盘读取真实的 tag 和 owner
    info.type     = get_resource_type(norm);
    info.owner_id = -1;
    info.group_id = -1;

    BlockDevice* dev = get_disk_driver();
    if (dev) {
        DirEntry entry;
        if (DiskFileSystem::find_entry(dev, norm, entry)) {
            InodeEntry ie;
            if (DiskFileSystem::inode_read(dev, entry.inode_id, ie)) {
                info.owner_id = static_cast<int>(ie.owner_id);
                if (ie.owner_id == 0xFFFFFFFF) info.owner_id = -1;
            }
        }
    }

    info.perms = get_default_perms(info.type, norm);
    return info;
}

// ============================================================
// 🚀 check_resource_access：访问权限检查（粗粒度）
// ============================================================
bool VFS::check_resource_access(const std::string& vfs_path) const {
    if (current_role == UserRole::R3_SYS_ADMIN) {
        return true;
    }

    ResourceType type = get_resource_type(vfs_path);

    switch (type) {
        case ResourceType::PUBLIC:
            return true;

        case ResourceType::SYSTEM:
            return static_cast<int>(current_role) <= static_cast<int>(UserRole::R2_HW_ADMIN);

        case ResourceType::PRIVATE: {
            std::string norm = normalize_path(vfs_path);

            if (static_cast<int>(current_role) <= static_cast<int>(UserRole::R1_SUPER_ADMIN)) {
                return true;
            }

            std::string own_home = "/home/" + std::to_string(current_user_id);
            if (norm == own_home || norm.rfind(own_home + "/", 0) == 0) {
                return true;
            }
            return false;
        }

        case ResourceType::SPECIAL:
            return static_cast<int>(current_role) <= static_cast<int>(UserRole::R3_SYS_ADMIN);

        case ResourceType::GROUP: {
            if (current_group_id == -1) return false;
            std::string norm = normalize_path(vfs_path);
            
            // 🚀 核心修复：遍历注册表必须加读锁
            std::shared_lock<std::shared_mutex> lock(vfs_mutex);
            for (const auto& [reg_path, info] : resource_registry) {
                if (norm == reg_path || norm.rfind(reg_path + "/", 0) == 0) {
                    return current_group_id == info.group_id;
                }
            }
            return false;
        }

        default:
            return false;
    }
}

// 注册资源（🚀 加写锁）
void VFS::register_special_resource(const std::string& vfs_path, ResourceType type, int group_id) {
    ResourceInfo info;
    info.type     = type;
    info.owner_id = current_user_id;
    info.group_id = group_id;
    info.perms    = PERM_ALL;
    
    std::string norm = normalize_path(vfs_path);
    std::unique_lock<std::shared_mutex> lock(vfs_mutex);
    resource_registry[norm] = info;
}

// 受保护路径（🚀 加写锁/读锁）
void VFS::add_protected_path(const std::string& vfs_path) {
    std::string norm = normalize_path(vfs_path);
    std::unique_lock<std::shared_mutex> lock(vfs_mutex);
    protected_paths.insert(norm);
}

void VFS::remove_protected_path(const std::string& vfs_path) {
    std::string norm = normalize_path(vfs_path);
    std::unique_lock<std::shared_mutex> lock(vfs_mutex);
    protected_paths.erase(norm);
}

bool VFS::is_protected_path(const std::string& vfs_path) const {
    std::string norm = normalize_path(vfs_path);
    std::shared_lock<std::shared_mutex> lock(vfs_mutex);
    return protected_paths.count(norm) > 0;
}

// ============================================================
// 🚀 get_disk_driver 实现
// ============================================================
BlockDevice* VFS::get_disk_driver() const {
    return disk_driver ? disk_driver.get() : nullptr;
}

// ============================================================
// 🚀 get_dir_size：递归统计目录下所有文件的字节总大小
// ============================================================
static uint64_t get_dir_size_internal(BlockDevice* disk, const std::string& norm, int depth) {
    if (depth > 64) {
        // 🚀 防爆栈：目录嵌套过深（防止恶意循环链接）
        return 0; 
    }

    uint64_t total = 0;

    // 🚀 根目录特判：/ 是 inode 2，没有目录项，不能走 find_entry
    if (norm != "/") {
        DirEntry self_entry;
        if (!DiskFileSystem::find_entry(disk, norm, self_entry)) return 0;
        if (self_entry.type != 1) return 0;
    }

    std::vector<std::string> items = DiskFileSystem::list_dir(disk, norm);

    for (const auto& raw : items) {
        if (raw.rfind("[文件] ", 0) == 0) {
            std::string pure = raw.substr(raw.find(' ') + 1);
            if (pure.empty()) continue;

            std::string child_path = norm;
            if (child_path.empty() || child_path.back() != '/') child_path += '/';
            child_path += pure;

            DirEntry entry;
            if (DiskFileSystem::find_entry(disk, child_path, entry)) {
                InodeEntry ie;
                if (DiskFileSystem::inode_read(disk, entry.inode_id, ie)) {
                    total += ie.size;
                }
            }
        }
        else if (raw.rfind("[目录] ", 0) == 0) {
            std::string pure = raw.substr(raw.find(' ') + 1);
            if (pure.empty()) continue;

            std::string child_path = norm;
            if (child_path.empty() || child_path.back() != '/') child_path += '/';
            child_path += pure;

            total += get_dir_size_internal(disk, child_path, depth + 1);
        }
    }

    return total;
}

uint64_t VFS::get_dir_size(const std::string& vfs_path) {
    std::string norm = normalize_path(vfs_path);
    BlockDevice* disk = get_disk_driver();
    if (!disk) return 0;

    // 🚀 将无锁查询移入 internal，防止对外部产生锁依赖
    return get_dir_size_internal(disk, norm, 0);
}