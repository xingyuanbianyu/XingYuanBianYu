#include "../include/vfs.h"
#include "disk_image_driver.h"
#include "disk_fs.h"
#include <iostream>
#include <cctype>
#include <shared_mutex>

// ============================================================
// 🚀 owner_id → 角色等级
// ============================================================
static UserRole owner_id_to_role(int id) {
    if (id < 0 || id > 300) return UserRole::R0_ULTIMATE;
    if (id == 0)            return UserRole::R3_SYS_ADMIN;
    if (id >= 1   && id <= 100) return UserRole::R4_GENERAL_ADMIN;
    if (id >= 101 && id <= 300) return UserRole::R5_MEMBER;
    return UserRole::R6_GUEST;
}

// ============================================================
// 🚀 删除权限判定
// ============================================================
static bool can_delete(UserRole cur,
                       UserRole owner_role,
                       int cur_user_id,
                       int target_owner_id,
                       ResourceType target_type) {
    if (cur == UserRole::R0_ULTIMATE) return true;

    if (cur == UserRole::R3_SYS_ADMIN) {
        return owner_role != UserRole::R0_ULTIMATE;
    }

    if (cur == UserRole::R4_GENERAL_ADMIN) {
        return owner_role != UserRole::R0_ULTIMATE &&
               owner_role != UserRole::R3_SYS_ADMIN;
    }

    if (cur == UserRole::R5_MEMBER) {
        return target_owner_id == cur_user_id;
    }

    if (cur == UserRole::R6_GUEST) {
        return target_type == ResourceType::PUBLIC;
    }

    return false;
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

// ================== 删除节点 ==================
std::string VFS::delete_node(const std::string& vfs_path) {
    std::string norm = normalize_path(vfs_path);

    // 🚀 受保护路径
    if (is_protected_path(norm)) {
        if (current_role != UserRole::R3_SYS_ADMIN &&
            current_role != UserRole::R0_ULTIMATE) {
            return "错误: 该文件为系统账户配置，只读（需要 R3 权限）";
        }
    }

    // ============================================================
    // 🚀 挂载点防护：不能删除挂载点本身，也不能删除包含挂载点的父目录！
    // ============================================================
    {
        std::shared_lock<std::shared_mutex> lock(vfs_mutex); // 遍历挂载表需加读锁
        for (const auto& [mp, entry] : mount_table) {
            // 规则 1：不能删除挂载点本身
            if (mp == norm) {
                return "错误: 挂载点不允许直接删除，请先卸载 ('drt <名字> -F')";
            }
            // 规则 2：不能删除挂载点的祖先目录（防止幽灵挂载）
            if (path_starts_with(mp, norm + "/")) {
                return "错误: 目录中包含已挂载的设备 (" + mp + ")，请先卸载";
            }
        }
    }

    // 根目录
    if (norm == "/") {
        return "错误: 无法删除根目录";
    }

    // 盘符防护
    bool is_drive = false;
    if (norm.size() >= 2) {
        char last = norm.back();
        char prev = norm[norm.size() - 2];
        if (last == ':' && std::isalpha((unsigned char)prev)) {
            is_drive = true;
        }
    }
    if (is_drive && get_role() != UserRole::R0_ULTIMATE) {
        return "错误: 删除盘符需要 R0 权限，请使用 'xyby del <盘符>'";
    }

    // ============================================================
    // 🚀 解析挂载点，决定用哪个 driver
    // ============================================================
    MountResolve mr = resolve_mount(norm);
    if (!mr.disk) {
        return "错误: 无法定位存储设备";
    }

    // 存在性检查（用正确的 driver）
    DirEntry entry;
    if (!DiskFileSystem::find_entry(mr.disk, mr.target_path, entry)) {
        return "错误: 路径不存在 (" + vfs_path + ")";
    }

    // ============================================================
    // 🚀 归属 + 资源类型校验
    // ============================================================
    {
        ResourceInfo ri     = get_resource_info(norm);
        ResourceType rtype  = get_resource_type(norm);

        UserRole cur        = get_role();
        UserRole owner_role = owner_id_to_role(ri.owner_id);
        int      cur_uid    = get_user_id();

        if (!can_delete(cur, owner_role, cur_uid, ri.owner_id, rtype)) {
            if (cur == UserRole::R6_GUEST) {
                return "权限不足: R6 访客只能操作公共资源";
            }
            return "权限不足: 无权删除他人文件";
        }
    }

    // 🚀 用正确的 driver 执行底层磁盘删除
    if (DiskFileSystem::delete_entry(mr.disk, mr.target_path)) {
        // 🚀 核心修复：清理 VFS 内存注册表必须加写锁！
        std::unique_lock<std::shared_mutex> lock(vfs_mutex);
        resource_registry.erase(norm);
        return "";
    }

    return "错误: 虚拟文件系统删除失败";
}