#include "../include/vfs.h"
#include <shared_mutex>

// ============================================================
// 🚀 VFS 核心状态 Getter / Setter 实现
// ============================================================

int VFS::get_user_id() const {
    // int 是基本类型，哪怕并发写一半，读出来也是合法的，不需要加锁
    return current_user_id;
}

UserRole VFS::get_role() const {
    return current_role;
}

void VFS::set_role(UserRole r) {
    current_role = r;
}

std::string VFS::get_cwd() const {
    std::shared_lock<std::shared_mutex> lock(vfs_mutex);
    return current_cwd;
}

void VFS::set_sudo_origin(int uid) {
    sudo_origin_uid = uid;
}

void VFS::clear_sudo_origin() {
    sudo_origin_uid = -9999;
}

int VFS::get_effective_owner() const {
    return (sudo_origin_uid != -9999) ? sudo_origin_uid : current_user_id;
}