#include "disk_fs.h"
#include "block_device.h"
#include <algorithm>
#include <sstream>
#include <vector>
#include <cstring>
#include <shared_mutex>
#include <thread>
#include <iostream>
#include <ctime>
#include <mutex>
#include <map>
#include <set>
// ================== 全局状态 ==================
// 🚀 文件系统的全局读写锁（保护底层的块和inode数据）
static std::shared_mutex g_fs_rwlock;

// 🚀 保护 map 内部结构的互斥锁（防止多线程插入/查找导致红黑树崩溃）
static std::mutex g_map_mutex;

// 🚀 每个盘一份独立 bitmap
static std::map<BlockDevice*, std::vector<uint8_t>> g_block_bitmaps;
static std::map<BlockDevice*, std::vector<uint8_t>> g_inode_bitmaps;
static std::map<BlockDevice*, uint32_t> g_total_blocks_map;

// 🚀 当前操作绑定的盘
static thread_local BlockDevice* g_current_disk = nullptr;

// ============================================================
// 🚀 RAII 上下文管理器：自动绑定和恢复当前盘
// ============================================================
class DiskScope {
    BlockDevice* old_disk;
public:
    explicit DiskScope(BlockDevice* disk) : old_disk(g_current_disk) {
        if (disk) g_current_disk = disk;
    }
    ~DiskScope() {
        g_current_disk = old_disk;
    }
};

// ============================================================
// 🚀 安全的全局资源获取（带锁 + 空指针保护）
// ============================================================
static std::vector<uint8_t>& block_bitmap_of(BlockDevice* disk) {
    static std::vector<uint8_t> dummy;
    if (!disk) return dummy;

    std::lock_guard<std::mutex> lock(g_map_mutex);
    return g_block_bitmaps[disk];
}

static std::vector<uint8_t>& inode_bitmap_of(BlockDevice* disk) {
    static std::vector<uint8_t> dummy;
    if (!disk) return dummy;

    std::lock_guard<std::mutex> lock(g_map_mutex);
    return g_inode_bitmaps[disk];
}

static uint32_t& total_blocks_of(BlockDevice* disk) {
    static uint32_t dummy = 0;
    if (!disk) return dummy;

    std::lock_guard<std::mutex> lock(g_map_mutex);
    return g_total_blocks_map[disk];
}

// 🚀 兼容旧的裸引用：g_block_bitmap → 当前盘的 bitmap
#define g_block_bitmap  block_bitmap_of(g_current_disk)
#define g_inode_bitmap  inode_bitmap_of(g_current_disk)

// ================== 按顺序 include 拆分文件 ==================
#include "disk_fs_inc/dfs_util.inc"
#include "disk_fs_inc/dfs_super.inc"
#include "disk_fs_inc/dfs_inode.inc"
#include "disk_fs_inc/dfs_index.inc"
#include "disk_fs_inc/dfs_domain.inc"
#include "disk_fs_inc/dfs_link.inc"
#include "disk_fs_inc/dfs_recovery.inc"
#include "disk_fs_inc/dfs_dir.inc"
#include "disk_fs_inc/dfs_file.inc"
#include "disk_fs_inc/dfs_ops.inc"

// 🚀 新增 7 大机制（顺序：作用块 → 域遍布 → 查树图 → 静态码 → 动态头 → 物理逻辑头）
#include "disk_fs_inc/dfs_scope.inc"
#include "disk_fs_inc/dfs_vbitmap.inc"
#include "disk_fs_inc/dfs_treemap.inc"
#include "disk_fs_inc/dfs_static.inc"
#include "disk_fs_inc/dfs_dynheader.inc"
#include "disk_fs_inc/dfs_plh.inc"