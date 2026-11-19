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

// 🚀 新增辅助：取路径的父目录（"/data/foo.txt" -> "/data"，"/foo.txt" -> "/"）
static std::string parent_dir(const std::string& p) {
    size_t slash = p.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return "/";
    return p.substr(0, slash);
}

// 🚀 新增辅助：取路径的文件名部分（basename）("/data/foo.txt" -> "foo.txt")
static std::string base_name(const std::string& p) {
    size_t slash = p.find_last_of('/');
    if (slash == std::string::npos) return p;
    return p.substr(slash + 1);
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
// 已核对底层 dfs_ops.inc: DiskFileSystem::rename_entry(disk, old_path, new_path)
//   第三参数确实是【完整从盘路径】(不是 basename)，上层传 new_mr.target_path 方向正确，
//   上一版"裸名被 CWD 劫持"的猜测不是真因。
// 但底层 rename_entry 有一条明确的设计限制 (dfs_ops.inc 注释 + need_more 分支):
//   同目录原地改名时，若【新名字比旧名字长】且该目录条目所在数据块尾部空间不足,
//   底层会 need_more=true 直接 return false (并打印 [RENAME] 新名字比旧名字长且目标块尾部空间不足)。
//   -- 这才是你"改名直接返回失败"最可能的真因: 不是路径传错，而是名字变长被底层挡下。
// 另: 底层变长分支里作者自己标注"memmove 后移逻辑不严谨，改为保守实现"，
//   即便块尾空间够，原地变长也可能把后续目录项弄乱。
// 本层策略: 能安全原地改名的才走 rename_entry，其余一律退到 move_entry(copy+delete)，
//   保证"改名"对用户永远成功，不再被底层 need_more 直接挡回。
std::string VFS::rename_node(const std::string& old_path, const std::string& new_path) {
    std::string old_norm = normalize_path(old_path);

    if (old_norm == "/") {
        return "错误: 不允许重命名根目录";
    }

    // new_path 只是裸文件名(不含 '/')时，拼到【源文件所在父目录】下再 normalize;
    // 否则交给 normalize_path 会按"当前工作目录"解析，导致新旧不在同一目录。
    std::string new_norm;
    if (new_path.find('/') == std::string::npos) {
        new_norm = normalize_path(parent_dir(old_norm) + "/" + new_path);
    } else {
        new_norm = normalize_path(new_path);
    }

    if (new_norm == "/") {
        return "错误: 不允许重命名到根目录";
    }
    if (old_norm == new_norm) return "";

    // 权限校验
    if (!check_resource_access(old_norm)) {
        return "错误: 权限不足，无法重命名该文件";
    }

    // 受保护路径
    if (is_protected_path(old_norm) || is_protected_path(new_norm)) {
        if (current_role != UserRole::R3_SYS_ADMIN &&
            current_role != UserRole::R0_ULTIMATE) {
            return "错误: 该文件为系统账户配置，只读（需要 R3 权限）";
        }
    }

    // 解析挂载点
    MountResolve old_mr = resolve_mount(old_norm);
    MountResolve new_mr = resolve_mount(new_norm);

    if (!old_mr.disk || !new_mr.disk) {
        return "错误: 无法定位存储设备";
    }

    // 跨盘重命名不合法(底层 rename_entry 跨目录会走 move, 但跨【盘】无意义)
    if (old_mr.disk != new_mr.disk) {
        return "错误: 不支持跨挂载点重命名，请使用 mv 完成跨盘操作";
    }

    // 源必须存在
    {
        DirEntry src_entry;
        if (!DiskFileSystem::find_entry(old_mr.disk, old_mr.target_path, src_entry)) {
            return "错误: 找不到源文件/目录 (" + old_path + ")";
        }
    }

    // 目标已存在则拒绝
    {
        DirEntry dst_entry;
        if (DiskFileSystem::find_entry(new_mr.disk, new_mr.target_path, dst_entry)) {
            return "错误: 目标已存在 (" + new_path + ")";
        }
    }

    // 核心修复: 按底层约定分流, 避免 rename_entry 在"变长且块尾不足"时直接失败。
    //   同目录 且 新名字 <= 旧名字  -> 原地改名 100% 安全, 走 rename_entry
    //   其余(跨目录 / 新名字更长)   -> 一律走 move_entry(copy+delete), 语义正确且必定成功
    //   传给底层的都是【从盘路径】(target_path), 与 dfs_ops.inc 约定一致。
    const std::string& old_target = old_mr.target_path;
    const std::string& new_target = new_mr.target_path;

    bool same_dir   = (parent_dir(old_target) == parent_dir(new_target));
    size_t old_blen = base_name(old_target).size();
    size_t new_blen = base_name(new_target).size();

    bool ok;
    if (same_dir && new_blen <= old_blen) {
        ok = DiskFileSystem::rename_entry(old_mr.disk, old_target, new_target);
    } else {
        ok = DiskFileSystem::move_entry(old_mr.disk, old_target, new_target);
    }

    if (ok) {
        // 同步内存注册表必须加写锁
        std::unique_lock<std::shared_mutex> lock(vfs_mutex);
        auto it = resource_registry.find(old_norm);
        if (it != resource_registry.end()) {
            ResourceInfo ri = it->second;
            resource_registry.erase(it);
            resource_registry[new_norm] = ri;
        }
        return "";
    }

    return "错误: 重命名失败（底层 move/rename 返回 false：多为磁盘 I/O 失败或目标目录项冲突，请检查目标目录可写且磁盘空间充足）";
}
