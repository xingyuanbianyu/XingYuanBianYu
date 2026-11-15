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

// ============================================================
// 🚀 辅助：更新注册表路径（并发安全）
// ============================================================
static void update_registry_path(VFS& vfs, const std::string& old_norm, const std::string& new_norm) {
    // 注意：这里不直接访问 vfs.resource_registry，因为它是 private 的。
    // 我们使用 vfs.set_resource_info 来安全地更新。
    ResourceInfo ri = vfs.get_resource_info(old_norm);
    // 如果这个路径没有在注册表里登记过（全都是默认值），就不用搬了
    if (ri.owner_id == -1 && ri.group_id == -1 && ri.type == ResourceType::PUBLIC) {
        // 简单判定逻辑，视具体业务而定
    }
    
    // 实际上由于 VFS 没有提供直接转移的 API，我们可以用 remove + set 模拟
    // 但为了不破坏原有逻辑，这里保留原有的直接操作，**但必须加锁**。
    // 我们需要一个 friend 机制或者 public 方法，最安全的做法是利用已有的 set_resource_info。
}

// ================== 移动节点 ==================
std::string VFS::move_node(const std::string& src_path, const std::string& dst_path) {
    std::string src_norm = normalize_path(src_path);
    std::string dst_norm = normalize_path(dst_path);

    if (src_norm == "/" || dst_norm == "/") {
        return "错误: 不允许移动根目录";
    }
    if (src_norm == dst_norm) return "";

    // 🚀 权限校验
    if (!check_resource_access(src_norm)) {
        return "错误: 权限不足，无法移动该文件";
    }
    if (!check_resource_access(dst_norm)) {
        return "错误: 权限不足，无法在目标位置写入";
    }

    // 🚀 受保护路径
    if (is_protected_path(src_norm) || is_protected_path(dst_norm)) {
        if (current_role != UserRole::R3_SYS_ADMIN &&
            current_role != UserRole::R0_ULTIMATE) {
            return "错误: 该文件为系统账户配置，只读（需要 R3 权限）";
        }
    }

    // 🚀 解析源和目标的挂载点
    MountResolve src_mr = resolve_mount(src_norm);
    MountResolve dst_mr = resolve_mount(dst_norm);

    if (!src_mr.disk || !dst_mr.disk) {
        return "错误: 无法定位存储设备";
    }

    // 🚀 目标已存在则拒绝
    {
        DirEntry dst_entry;
        if (DiskFileSystem::find_entry(dst_mr.disk, dst_mr.target_path, dst_entry)) {
            return "错误: 目标已存在 (" + dst_path + ")";
        }
    }

    // ============================================================
    // 🚀 情形 1：源和目标都在同一个盘 → 直接底层 move
    // ============================================================
    if (src_mr.disk == dst_mr.disk) {
        if (DiskFileSystem::move_entry(src_mr.disk, src_mr.target_path, dst_mr.target_path)) {
            // 🚀 核心修复：同步内存注册表必须加写锁！
            std::unique_lock<std::shared_mutex> lock(vfs_mutex);
            auto it = resource_registry.find(src_norm);
            if (it != resource_registry.end()) {
                ResourceInfo ri = it->second;
                resource_registry.erase(it);
                resource_registry[dst_norm] = ri;
            }
            return "";
        }
        return "错误: 移动失败";
    }

    // ============================================================
    // 🚀 情形 2：跨盘移动 → 读源 + 写目标 + 删源
    // ============================================================
    DirEntry src_entry;
    if (!DiskFileSystem::find_entry(src_mr.disk, src_mr.target_path, src_entry)) {
        return "错误: 找不到源文件/目录 (" + src_path + ")";
    }
    if (src_entry.type != 0) {
        return "错误: 跨盘移动目录暂不支持";
    }

    // 🚀 核心修复：跨盘 OOM 防御，限制最大 16MB
    if (src_entry.size > 16 * 1024 * 1024) {
        return "错误: 文件超过 16MB，暂不支持跨盘移动（防止系统内存溢出）";
    }

    std::string content = DiskFileSystem::read_file_content(src_mr.disk, src_mr.target_path);

    if (!DiskFileSystem::create_file(dst_mr.disk, dst_mr.target_path)) {
        return "错误: 目标创建失败";
    }
    if (!DiskFileSystem::write_file(dst_mr.disk, dst_mr.target_path, content)) {
        DiskFileSystem::delete_entry(dst_mr.disk, dst_mr.target_path);
        return "错误: 目标写入失败";
    }

    // 删除源
    DiskFileSystem::delete_entry(src_mr.disk, src_mr.target_path);
    
    // 🚀 同步注册表加写锁
    std::unique_lock<std::shared_mutex> lock(vfs_mutex);
    resource_registry.erase(src_norm);
    return "";
}

// ================== 复制节点 ==================
std::string VFS::copy_node(const std::string& src_path, const std::string& dst_path) {
    std::string src_norm = normalize_path(src_path);
    std::string dst_norm = normalize_path(dst_path);

    if (src_norm == "/" || dst_norm == "/") {
        return "错误: 不允许复制根目录";
    }
    if (src_norm == dst_norm) {
        return "错误: 源和目标是同一个路径";
    }

    // 🚀 权限校验
    if (!check_resource_access(src_norm)) {
        return "错误: 权限不足，无法读取源文件";
    }
    if (!check_resource_access(dst_norm)) {
        return "错误: 权限不足，无法在目标位置写入";
    }

    // 🚀 受保护路径
    if (is_protected_path(src_norm) || is_protected_path(dst_norm)) {
        if (current_role != UserRole::R3_SYS_ADMIN &&
            current_role != UserRole::R0_ULTIMATE) {
            return "错误: 该文件为系统账户配置，只读（需要 R3 权限）";
        }
    }

    // 🚀 解析挂载点
    MountResolve src_mr = resolve_mount(src_norm);
    MountResolve dst_mr = resolve_mount(dst_norm);

    if (!src_mr.disk || !dst_mr.disk) {
        return "错误: 无法定位存储设备";
    }

    // 🚀 目标已存在则拒绝
    {
        DirEntry dst_entry;
        if (DiskFileSystem::find_entry(dst_mr.disk, dst_mr.target_path, dst_entry)) {
            return "错误: 目标已存在 (" + dst_path + ")";
        }
    }

    // ============================================================
    // 🚀 情形 1：源和目标都在同一个盘 → 直接底层 copy
    // ============================================================
    if (src_mr.disk == dst_mr.disk) {
        if (DiskFileSystem::copy_entry(src_mr.disk, src_mr.target_path, dst_mr.target_path)) {
            return "";
        }
        return "错误: 复制失败";
    }

    // ============================================================
    // 🚀 情形 2：跨盘复制 → 读源 + 写目标
    // ============================================================
    DirEntry src_entry;
    if (!DiskFileSystem::find_entry(src_mr.disk, src_mr.target_path, src_entry)) {
        return "错误: 找不到源文件/目录 (" + src_path + ")";
    }
    if (src_entry.type != 0) {
        return "错误: 跨盘复制目录暂不支持";
    }

    // 🚀 核心修复：跨盘 OOM 防御，限制最大 16MB
    if (src_entry.size > 16 * 1024 * 1024) {
        return "错误: 文件超过 16MB，暂不支持跨盘复制（防止系统内存溢出）";
    }

    std::string content = DiskFileSystem::read_file_content(src_mr.disk, src_mr.target_path);

    if (!DiskFileSystem::create_file(dst_mr.disk, dst_mr.target_path)) {
        return "错误: 目标创建失败";
    }
    if (!DiskFileSystem::write_file(dst_mr.disk, dst_mr.target_path, content)) {
        DiskFileSystem::delete_entry(dst_mr.disk, dst_mr.target_path);
        return "错误: 目标写入失败";
    }

    return "";
}

// ================== 重命名节点 ==================
std::string VFS::rename_node(const std::string& old_path, const std::string& new_path) {
    std::string old_norm = normalize_path(old_path);
    std::string new_norm = normalize_path(new_path);

    if (old_norm == "/" || new_norm == "/") {
        return "错误: 不允许重命名根目录";
    }
    if (old_norm == new_norm) return "";

    // 🚀 权限校验
    if (!check_resource_access(old_norm)) {
        return "错误: 权限不足，无法重命名该文件";
    }

    // 🚀 受保护路径
    if (is_protected_path(old_norm) || is_protected_path(new_norm)) {
        if (current_role != UserRole::R3_SYS_ADMIN &&
            current_role != UserRole::R0_ULTIMATE) {
            return "错误: 该文件为系统账户配置，只读（需要 R3 权限）";
        }
    }

    // 🚀 解析挂载点
    MountResolve old_mr = resolve_mount(old_norm);
    MountResolve new_mr = resolve_mount(new_norm);

    if (!old_mr.disk || !new_mr.disk) {
        return "错误: 无法定位存储设备";
    }

    // 🚀 跨盘重命名不合法
    if (old_mr.disk != new_mr.disk) {
        return "错误: 不支持跨挂载点重命名，请使用 mv 完成跨盘操作";
    }

    // 🚀 目标已存在则拒绝
    {
        DirEntry dst_entry;
        if (DiskFileSystem::find_entry(new_mr.disk, new_mr.target_path, dst_entry)) {
            return "错误: 目标已存在 (" + new_path + ")";
        }
    }

    // 🚀 用正确的 driver 重命名
    if (DiskFileSystem::rename_entry(old_mr.disk, old_mr.target_path, new_mr.target_path)) {
        // 🚀 核心修复：同步内存注册表必须加写锁！
        std::unique_lock<std::shared_mutex> lock(vfs_mutex);
        auto it = resource_registry.find(old_norm);
        if (it != resource_registry.end()) {
            ResourceInfo ri = it->second;
            resource_registry.erase(it);
            resource_registry[new_norm] = ri;
        }
        return "";
    }

    return "错误: 重命名失败";
}