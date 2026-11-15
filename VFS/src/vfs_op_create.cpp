#include "../include/vfs.h"
#include "disk_image_driver.h"
#include "disk_fs.h"
#include <iostream>
#include <cctype> // 🚀 用于 isalpha

// 🚀 查家目录主人 uid（定义在 dcl_drt.cpp）
extern int drt_get_uid_by_name(const std::string& name);

// ============================================================
// 🚀 计算归属（兼容带盘符的路径 /C:/home/alice）
// ============================================================
static int calc_owner_id(const std::string& norm) {
    std::string view = norm;
    
    // 🚀 修复：剥离可能存在的盘符前缀，如 "/C:/home/xxx" -> "/home/xxx"
    if (view.size() >= 4 && view[0] == '/' && std::isalpha((unsigned char)view[1]) && view[2] == ':' && view[3] == '/') {
        view = view.substr(3);
    }

    if (view.rfind("/home/", 0) == 0) {
        std::string rest = view.substr(6);
        size_t slash = rest.find('/');
        std::string hn = (slash == std::string::npos) ? rest : rest.substr(0, slash);
        if (!hn.empty()) {
            int hid = drt_get_uid_by_name(hn);
            if (hid != -9999) return hid;
        }
    }
    return VFS::getInstance().get_user_id();
}

// ============================================================
// 🚀 计算资源类型：继承父目录的 resource_tag
// ============================================================
static ResourceType calc_resource_type(BlockDevice* disk, const std::string& parent_path) {
    if (!disk) return ResourceType::PUBLIC;

    std::string tag = DiskFileSystem::get_resource_tag(disk, parent_path);
    if (tag == RES_TAG_R0)    return ResourceType::SYSTEM;
    if (tag == RES_TAG_HOME)  return ResourceType::PUBLIC;
    if (tag == RES_TAG_ROOT)  return ResourceType::PRIVATE;
    if (tag == RES_TAG_CMD)   return ResourceType::SPECIAL;
    if (tag == RES_TAG_PATH)  return ResourceType::SYSTEM;

    return ResourceType::PUBLIC;
}

// ============================================================
// 🚀 父目录存在性 + 目录类型校验（含根目录特判）
// ============================================================
static std::string check_parent_dir(BlockDevice* disk, const std::string& parent_path) {
    if (parent_path == "/" || parent_path.empty()) {
        return "";
    }

    DirEntry parent_entry;
    if (!DiskFileSystem::find_entry(disk, parent_path, parent_entry)) {
        return "错误: 父目录不存在";
    }
    if (parent_entry.type != 1) {
        return "错误: 父路径不是一个目录";
    }
    return "";
}

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

// ================== 创建文件 ==================
std::string VFS::create_file(const std::string& vfs_path, const std::string& content) {
    std::string norm = normalize_path(vfs_path);

    // 🚀 权限校验
    if (!check_resource_access(norm)) {
        return "错误: 权限不足，无法在此位置创建文件";
    }

    if (is_protected_path(norm)) {
        return "错误: 该文件为系统账户配置，只读";
    }

    // 🚀 解析挂载点，决定用哪个 driver 和从盘子路径
    MountResolve mr = resolve_mount(norm);
    if (!mr.disk) {
        return "错误: 无法定位存储设备";
    }

    // 🚀 用正确的 target_path 求父目录
    std::string parent_path = DiskFileSystem::get_parent_path(mr.target_path);
    if (mr.target_path == "/" || parent_path.empty()) {
        return "错误: 无法在盘根创建文件";
    }

    // 🚀 父目录校验（用正确的 driver）
    std::string perr = check_parent_dir(mr.disk, parent_path);
    if (!perr.empty()) return perr;

    // 🚀 用正确的 driver 检查文件是否已存在
    DirEntry entry;
    if (DiskFileSystem::find_entry(mr.disk, mr.target_path, entry)) {
        return "错误: 文件已存在";
    }

    // 🚀 用正确的 driver 创建文件
    if (!DiskFileSystem::create_file(mr.disk, mr.target_path)) {
        return "错误: 虚拟文件系统创建文件失败";
    }

    // 🚀 核心修复：检查写入返回值，一旦失败立刻回滚删除空文件
    if (!content.empty()) {
        if (!DiskFileSystem::write_file(mr.disk, mr.target_path, content)) {
            DiskFileSystem::delete_entry(mr.disk, mr.target_path);
            return "错误: 写入初始内容失败，已撤销创建";
        }
    }

    // 🚀 固化归属和资源类型
    {
        ResourceInfo ri;
        ri.owner_id = calc_owner_id(norm);
        ri.group_id = -1;
        ri.type     = calc_resource_type(mr.disk, parent_path);
        ri.perms    = PERM_ALL;
        set_resource_info(norm, ri); // 内部自带锁保护
    }

    return "";
}

// ================== 创建目录 ==================
std::string VFS::create_directory(const std::string& vfs_path) {
    std::string norm = normalize_path(vfs_path);

    // 🚀 权限校验
    if (!check_resource_access(norm)) {
        return "错误: 权限不足，无法在此位置创建目录";
    }

    if (is_protected_path(norm)) {
        return "错误: 该路径为系统账户配置，只读";
    }

    // 🚀 解析挂载点
    MountResolve mr = resolve_mount(norm);
    if (!mr.disk) {
        return "错误: 无法定位存储设备";
    }

    // 🚀 用正确的 target_path 求父目录
    std::string parent_path = DiskFileSystem::get_parent_path(mr.target_path);
    if (mr.target_path == "/" || parent_path.empty()) {
        return "错误: 无法创建盘根";
    }

    // 🚀 父目录校验
    std::string perr = check_parent_dir(mr.disk, parent_path);
    if (!perr.empty()) return perr;

    // 🚀 用正确的 driver 检查目录是否已存在
    DirEntry entry;
    if (DiskFileSystem::find_entry(mr.disk, mr.target_path, entry)) {
        return "错误: 目录已存在";
    }

    // 🚀 用正确的 driver 创建目录
    // （底层 create_directory 内部如果有任何失败，自己会完成 inode 和 block 的释放回滚）
    if (!DiskFileSystem::create_directory(mr.disk, mr.target_path)) {
        return "错误: 虚拟文件系统创建目录失败";
    }

    // 🚀 固化归属和资源类型
    {
        ResourceInfo ri;
        ri.owner_id = calc_owner_id(norm);
        ri.group_id = -1;
        ri.type     = calc_resource_type(mr.disk, parent_path);
        ri.perms    = PERM_ALL;
        set_resource_info(norm, ri);
    }

    return "";
}