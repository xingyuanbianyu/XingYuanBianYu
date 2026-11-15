// ============================================================
// dfs_util.inc —— 底层工具、位图、路径辅助（per-disk，防死锁，防越界）
// ============================================================

// 🚀 移除了 #include "disk_fs.h"，因为主 .cpp 已经包含了

// 🚀 移除了重复的 METADATA_FIXED_END 和 METADATA_FALLBACK_START 定义
// （disk_fs.h 中已定义 DATA_START_BLOCK_DEF = 40）

// data_start 缓存（per-disk）
static std::map<BlockDevice*, uint32_t> g_data_start_map;

// 供 format / mount 刷新 data_start
static void data_start_invalidate(BlockDevice* disk) {
    if (!disk) return;
    std::lock_guard<std::mutex> lock(g_map_mutex);
    g_data_start_map.erase(disk);
}

static uint32_t data_start_of(BlockDevice* disk) {
    if (!disk) return DATA_START_BLOCK_DEF;

    {
        std::lock_guard<std::mutex> lock(g_map_mutex);
        auto it = g_data_start_map.find(disk);
        // 🚀 修复：改用 >= 以便命中 40 这个真实的 data_start 缓存
        if (it != g_data_start_map.end() && it->second >= DATA_START_BLOCK_DEF)
            return it->second;
    }

    uint8_t buf[BLOCK_SIZE] = {0};
    uint32_t ds = DATA_START_BLOCK_DEF;
    if (disk->read_sector(SUPERBLOCK_BLOCK, buf)) { // 🚀 修复：read_sector
        Superblock sb{};
        std::memcpy(&sb, buf, sizeof(sb));
        if (sb.magic == SUPER_MAGIC && sb.data_start >= DATA_START_BLOCK_DEF)
            ds = sb.data_start;
    }

    std::lock_guard<std::mutex> lock(g_map_mutex);
    g_data_start_map[disk] = ds;
    return ds;
}

static bool is_block_valid(BlockDevice* disk, uint32_t b) {
    if (!disk) return false;
    uint32_t total = total_blocks_of(disk);
    uint32_t ds    = data_start_of(disk);
    if (total <= RESERVED_BLOCKS + ds) return false;
    return b >= ds && b < total - RESERVED_BLOCKS;
}

static bool is_block_valid(uint32_t b) {
    return is_block_valid(g_current_disk, b);
}

// 定长名字（part_name / volume_label 等）
static std::string safe_name(const char* buf, size_t max_len) {
    if (!buf) return "";
    size_t len = strnlen(buf, max_len);
    if (len == 0) return "";
    return std::string(buf, len);
}

// ============================================================
// 🚀 块位图（per-disk）
// ============================================================
static bool block_bitmap_test(uint32_t b) {
    BlockDevice* disk = g_current_disk;
    if (!disk) return false;

    uint32_t total = total_blocks_of(disk);
    if (b >= total) return false;

    auto& bm = block_bitmap_of(disk);
    if (b / 8 >= bm.size()) return false;
    return (bm[b / 8] & (1u << (b % 8))) != 0;
}

static void block_bitmap_set(uint32_t b, bool used) {
    BlockDevice* disk = g_current_disk;
    if (!disk) return;

    uint32_t total = total_blocks_of(disk);
    if (b >= total) return;

    auto& bm = block_bitmap_of(disk);
    if (b / 8 >= bm.size()) bm.resize((b / 8) + 1, 0);

    if (used) bm[b / 8] |=  (uint8_t)(1u << (b % 8));
    else      bm[b / 8] &= (uint8_t)~(1u << (b % 8));
}

// ============================================================
// 校验和
// ============================================================
uint32_t DiskFileSystem::simple_checksum(const uint8_t* data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum = (sum << 5) ^ (sum >> 27) ^ data[i];
    }
    return sum;
}

// ============================================================
// 🚀 inode 位图（per-disk）
// ============================================================
bool DiskFileSystem::inode_bitmap_test(BlockDevice* disk, uint32_t inode_id) {
    if (!disk) return false;
    DiskScope scope(disk);

    auto& bm = inode_bitmap_of(disk);
    if (inode_id / 8 >= bm.size()) return false;
    return (bm[inode_id / 8] & (1u << (inode_id % 8))) != 0;
}

void DiskFileSystem::inode_bitmap_set(BlockDevice* disk, uint32_t inode_id, bool used) {
    if (!disk) return;
    DiskScope scope(disk);

    auto& bm = inode_bitmap_of(disk);
    if (inode_id / 8 >= bm.size()) bm.resize((inode_id / 8) + 1, 0);

    if (used) bm[inode_id / 8] |=  (uint8_t)(1u << (inode_id % 8));
    else      bm[inode_id / 8] &= (uint8_t)~(1u << (inode_id % 8));
}

// ============================================================
// 🚀 块分配 / 释放
// 🚀 关键约束：数据块只在 [data_start, total - RESERVED_BLOCKS) 之间分配
// ============================================================
static std::map<BlockDevice*, uint32_t> g_alloc_hint;
static std::mutex g_alloc_mutex;

// 供 format 调用，重置 hint 与 data_start 缓存
static void alloc_state_invalidate(BlockDevice* disk) {
    if (!disk) return;
    {
        std::lock_guard<std::mutex> lock(g_alloc_mutex);
        g_alloc_hint.erase(disk);
    }
    data_start_invalidate(disk);
}

uint32_t DiskFileSystem::allocate_sector(BlockDevice* disk) {
    if (!disk) return 0;
    DiskScope scope(disk);

    std::lock_guard<std::mutex> lock(g_alloc_mutex);

    uint32_t total = total_blocks_of(disk);
    uint32_t ds    = data_start_of(disk);
    if (total <= RESERVED_BLOCKS + ds + 1) return 0;   // 🚀 防下溢 + 至少 1 个数据块

    uint32_t limit = total - RESERVED_BLOCKS;
    uint32_t start_idx = g_alloc_hint[disk];
    if (start_idx < ds || start_idx >= limit) start_idx = ds;

    for (uint32_t i = start_idx; i < limit; ++i) {
        if (!block_bitmap_test(i)) {
            block_bitmap_set(i, true);
            g_alloc_hint[disk] = i + 1;
            return i;
        }
    }
    for (uint32_t i = ds; i < start_idx; ++i) {
        if (!block_bitmap_test(i)) {
            block_bitmap_set(i, true);
            g_alloc_hint[disk] = i + 1;
            return i;
        }
    }
    return 0;
}

void DiskFileSystem::free_sector(BlockDevice* disk, uint32_t sector) {
    if (!disk) return;
    DiskScope scope(disk);

    std::lock_guard<std::mutex> lock(g_alloc_mutex);

    uint32_t total = total_blocks_of(disk);
    uint32_t ds    = data_start_of(disk);
    if (total <= RESERVED_BLOCKS + ds) return;                  // 🚀 防下溢
    if (sector < ds || sector >= total - RESERVED_BLOCKS) return;

    block_bitmap_set(sector, false);
    if (sector < g_alloc_hint[disk]) g_alloc_hint[disk] = sector;
}

// ============================================================
// 路径辅助
// ============================================================
std::string DiskFileSystem::get_filename(const std::string& vfs_path) {
    std::string p = vfs_path;
    while (p.length() > 1 && p.back() == '/') p.pop_back();
    if (p == "/") return "/";
    size_t pos = p.find_last_of('/');
    if (pos == std::string::npos) return p;
    return p.substr(pos + 1);
}

std::string DiskFileSystem::get_parent_path(const std::string& vfs_path) {
    std::string p = vfs_path;
    while (p.length() > 1 && p.back() == '/') p.pop_back();
    size_t pos = p.find_last_of('/');
    if (pos == std::string::npos || pos == 0) return "/";
    return p.substr(0, pos);
}