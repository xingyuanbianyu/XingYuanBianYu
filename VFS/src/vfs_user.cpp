#include "../include/vfs.h" // 如果报错，可改为 #include "vfs.h"
#include <algorithm>
#include <mutex>
#include <shared_mutex>

// ================= VFS 权限系统实现 =================

// 1. 设置内置标识符，并自动映射对应角色
void VFS::set_user_id(int id) {
    // 🚀 边界防护：防止用户通过传入异常 ID 绕过权限
    if (id < -3) {
        current_user_id = -9999;
        current_role = UserRole::R6_GUEST;
        return;
    }
    if (id > 300) {
        current_user_id = id;
        current_role = UserRole::R6_GUEST;
        return;
    }

    current_user_id = id;

    if (id < 0) {
        if (id == -1)      current_role = UserRole::R2_HW_ADMIN;
        else if (id == -2) current_role = UserRole::R1_SUPER_ADMIN;
        else if (id == -3) current_role = UserRole::R0_ULTIMATE;
        else               current_role = UserRole::R6_GUEST;
    } else if (id == 0) {
        current_role = UserRole::R3_SYS_ADMIN;
    } else if (id >= 1 && id <= 100) {
        current_role = UserRole::R4_GENERAL_ADMIN;
    } else if (id >= 101 && id <= 300) {
        current_role = UserRole::R5_MEMBER;
    } else {
        current_role = UserRole::R6_GUEST;
    }
}

// 2. 获取角色中文名
std::string VFS::get_role_name() const {
    switch (current_role) {
        case UserRole::R0_ULTIMATE:      return "R0_究极管理员";
        case UserRole::R1_SUPER_ADMIN:   return "R1_超级管理员";
        case UserRole::R2_HW_ADMIN:      return "R2_高级管理员";
        case UserRole::R3_SYS_ADMIN:     return "R3_系统管理员";
        case UserRole::R4_GENERAL_ADMIN: return "R4_普通管理员";
        case UserRole::R5_MEMBER:        return "R5_普通成员";
        case UserRole::R6_GUEST:         return "R6_访客";
        default:                         return "Unknown";
    }
}

// 3. 检查指定权限等级是否满足（数字越小权限越高）
bool VFS::check_permission(UserRole required_role) const {
    return static_cast<int>(current_role) <= static_cast<int>(required_role);
}

// 4. 根据命令名称，检查是否允许执行
bool VFS::check_cmd_permission(const std::string& cmd_name) const {
    // 🚀 R3 及以上是系统管理员，拥有所有指令的最高执行权限
    if (static_cast<int>(current_role) <= static_cast<int>(UserRole::R3_SYS_ADMIN)) {
        return true;
    }

    // 🚀 防提权绕过：剥离绝对路径或相对路径前缀，只取纯指令名
    std::string pure_cmd = cmd_name;
    size_t slash_pos = pure_cmd.find_last_of("/\\");
    if (slash_pos != std::string::npos) {
        pure_cmd = pure_cmd.substr(slash_pos + 1);
    }

    static const std::map<std::string, UserRole> CMD_ROLE_MAP = {
        {"ui",   UserRole::R1_SUPER_ADMIN},
        {"kill", UserRole::R2_HW_ADMIN},
        {"ioq",  UserRole::R3_SYS_ADMIN},
        {"del",  UserRole::R3_SYS_ADMIN},
        {"run",  UserRole::R4_GENERAL_ADMIN},
        {"sft",  UserRole::R5_MEMBER},
    };

    auto it = CMD_ROLE_MAP.find(pure_cmd);
    if (it == CMD_ROLE_MAP.end()) {
        return true;
    }
    return check_permission(it->second);
}

// ============================================================
// 🚀 旧版 get_resource_info：转发到动态版
// ============================================================
ResourceInfo VFS::get_resource_info(const std::string& vfs_path) const {
    return get_resource_info_dynamic(vfs_path);
}

// ============================================================
// 🚀 注意：set_resource_info 的新版包含底层磁盘同步，已移至 vfs_resource.cpp！
//    请勿在此文件重新定义，否则会引发 duplicate symbol 链接错误。
// ============================================================

// ============================================================
// 🚀 check_perm：细粒度权限位检查
//    R0~R2 直接放行；其余角色按动态权限位逐位比对
// ============================================================
bool VFS::check_perm(const std::string& vfs_path, uint32_t perm) const {
    // R0~R2：绝对权限，直接放行
    if (static_cast<int>(current_role) <= static_cast<int>(UserRole::R2_HW_ADMIN)) {
        return true;
    }

    // get_resource_info_dynamic 内部应该是线程安全的（加了读锁）
    ResourceInfo ri = get_resource_info_dynamic(vfs_path);
    return (ri.perms & perm) == perm;
}

// ============================================================
// 🚀 set_perm：修改权限位
//    🚀 核心修复 1：加写锁保护 resource_registry。
//    🚀 核心修复 2：防止并发丢失更新 (Lost Update)。
// ============================================================
void VFS::set_perm(const std::string& vfs_path, uint32_t perm) {
    std::string norm = normalize_path(vfs_path);

    // 由于我们不知道 get_resource_info_dynamic 内部是否有锁，
    // 为了绝对防范死锁，我们先用无锁/独立作用域读取它
    ResourceInfo ri;
    {
        ri = get_resource_info_dynamic(norm);
    }

    // 只修改 perms 字段
    ri.perms = perm;

    // 写回注册表（加写锁，保护 map 树结构）
    std::unique_lock<std::shared_mutex> lock(vfs_mutex);
    resource_registry[norm] = ri;
}