// ==========================================================
// 🚀 物理逻辑头 (Physical Logical Header) 实现 v3
// 布局（与 disk_fs.h 对齐）：
//   block 12..19 : PhysicalLogicalHeader[0..7]（每分区一块）
// 语义：
//   - format 时不生成（"至少分一次盘"）
//   - 第一次 plh_generate 触发时**全量重写**（幂等）
//   - plh_sync_from_partition 用于分区表变更后合并同步
// ==========================================================

// ---------- 内部无锁 helper ----------

// 绕过 read_partition_table 的锁，直接读块
static bool plh_read_partition_map_unlocked(BlockDevice* disk, PhysicalLogicalMap& pmap) {
    if (!disk) return false;
    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(PARTITION_TABLE_BLOCK, buf)) return false;
    std::memcpy(&pmap, buf, sizeof(pmap));
    if (pmap.magic != PART_MAGIC) return false;
    if (pmap.part_count > MAX_PARTITIONS) return false;
    return true;
}

// 绕过 read_super 的锁，直接读块
static bool plh_read_super_unlocked(BlockDevice* disk, Superblock& sb) {
    if (!disk) return false;
    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(SUPERBLOCK_BLOCK, buf)) return false;
    std::memcpy(&sb, buf, sizeof(sb));
    return sb.magic == SUPER_MAGIC;
}

static bool plh_read_unlocked(BlockDevice* disk, uint32_t part_index,
                              PhysicalLogicalHeader& out) {
    if (!disk || part_index >= MAX_PARTITIONS) return false;
    
    uint32_t blk = PLH_TABLE_BLOCK + part_index;
    if (!is_block_valid(disk, blk)) return false;

    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(blk, buf)) return false;
    std::memcpy(&out, buf, sizeof(out));
    return out.magic == PLH_MAGIC && out.part_index == part_index;
}

static bool plh_write_unlocked(BlockDevice* disk, uint32_t part_index,
                               const PhysicalLogicalHeader& in) {
    if (!disk || part_index >= MAX_PARTITIONS) return false;
    // 🚀 拒写脏数据：magic / part_index 必须与目标槽位匹配
    if (in.magic != PLH_MAGIC) return false;
    if (in.part_index != part_index) return false;

    uint32_t blk = PLH_TABLE_BLOCK + part_index;
    if (!is_block_valid(disk, blk)) return false;

    uint8_t buf[BLOCK_SIZE] = {0};
    std::memcpy(buf, &in, sizeof(in));
    return disk->write_sector(blk, buf);
}

// 构造分区 i 的默认 PLH
static PhysicalLogicalHeader plh_build_default(const PhysicalLogicalMap& pmap,
                                                const Superblock& sb,
                                                uint32_t i) {
    PhysicalLogicalHeader h{};
    h.magic       = PLH_MAGIC;
    h.part_index  = i;
    h.start_block = pmap.parts[i].start_block;
    h.end_block   = pmap.parts[i].end_block;

    // 🚀 核心修复：所有分区共享主盘的 inode 表！
    // 因为我们不格式化子分区，所以子分区必须借用主盘的 inode 字典。
    h.inode_table_start  = sb.inode_table_start;
    h.inode_table_blocks = sb.inode_table_blocks;
    
    // 统一标记为已挂载可用状态
    h.flags              = 0x1;
    
    // 🚀 用分区表里的标签作为 tag，方便挂载时识别
    std::strncpy(h.tag, pmap.parts[i].part_name, sizeof(h.tag) - 1);
    
    return h;
}

// ---------- 公开 API ----------

bool DiskFileSystem::plh_read(BlockDevice* disk, uint32_t part_index,
                              PhysicalLogicalHeader& out) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::shared_lock<std::shared_mutex> lk(g_fs_rwlock);
    return plh_read_unlocked(disk, part_index, out);
}

bool DiskFileSystem::plh_write(BlockDevice* disk, uint32_t part_index,
                               const PhysicalLogicalHeader& in) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::unique_lock<std::shared_mutex> lk(g_fs_rwlock);
    return plh_write_unlocked(disk, part_index, in);
}

// 🚀 检查所有已声明分区都有有效 PLH
bool DiskFileSystem::plh_exists(BlockDevice* disk) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::shared_lock<std::shared_mutex> lk(g_fs_rwlock);

    PhysicalLogicalMap pmap{};
    if (!plh_read_partition_map_unlocked(disk, pmap)) return false;
    if (pmap.part_count == 0) return false;

    for (uint32_t i = 0; i < pmap.part_count; ++i) {
        PhysicalLogicalHeader h{};
        if (!plh_read_unlocked(disk, i, h)) return false;
    }
    return true;
}

bool DiskFileSystem::plh_generate(BlockDevice* disk) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::unique_lock<std::shared_mutex> lk(g_fs_rwlock);

    // 🚀 幂等：先做一次完整的"存在性检查"（内联，不调 plh_exists 以避免锁重入）
    {
        PhysicalLogicalMap pmap0{};
        if (plh_read_partition_map_unlocked(disk, pmap0) && pmap0.part_count > 0) {
            bool all_ok = true;
            for (uint32_t i = 0; i < pmap0.part_count; ++i) {
                PhysicalLogicalHeader h{};
                if (!plh_read_unlocked(disk, i, h)) { all_ok = false; break; }
            }
            if (all_ok) return true;
        }
    }

    PhysicalLogicalMap pmap{};
    if (!plh_read_partition_map_unlocked(disk, pmap)) return false;

    // 🚀 放宽校验：只要有分区（哪怕只有 1 个）就能生成 PLH
    if (pmap.part_count == 0) {
        std::cerr << "[PLH] 未发现任何分区，不生成物理逻辑头\n";
        return false;
    }

    Superblock sb{};
    if (!plh_read_super_unlocked(disk, sb)) {
        std::cerr << "[PLH] 超级块无效，拒绝生成\n";
        return false;
    }

    // 🚀 全量重写：一次 unique_lock 内完成，避免部分失败导致状态不一致
    for (uint32_t i = 0; i < pmap.part_count; ++i) {
        PhysicalLogicalHeader h = plh_build_default(pmap, sb, i);
        if (!plh_write_unlocked(disk, i, h)) {
            std::cerr << "[PLH] 分区 " << i << " 写入失败，操作中止\n";
            return false;
        }
    }
    return true;
}

bool DiskFileSystem::plh_sync_from_partition(BlockDevice* disk) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::unique_lock<std::shared_mutex> lk(g_fs_rwlock);

    PhysicalLogicalMap pmap{};
    if (!plh_read_partition_map_unlocked(disk, pmap)) return false;

    Superblock sb{};
    if (!plh_read_super_unlocked(disk, sb)) return false;

    // 🚀 合并语义：保留已有 PLH 的其他字段，只刷新 start/end
    for (uint32_t i = 0; i < pmap.part_count; ++i) {
        PhysicalLogicalHeader h{};
        if (!plh_read_unlocked(disk, i, h)) {
            h = plh_build_default(pmap, sb, i);   // 不存在则初始化
        }
        h.start_block = pmap.parts[i].start_block;
        h.end_block   = pmap.parts[i].end_block;

        // 🚀 所有的 inode 表都必须跟随主 SB 刷新，保证所有子分区共享字典
        h.inode_table_start  = sb.inode_table_start;
        h.inode_table_blocks = sb.inode_table_blocks;
        
        // 更新标签
        std::strncpy(h.tag, pmap.parts[i].part_name, sizeof(h.tag) - 1);

        if (!plh_write_unlocked(disk, i, h)) return false;
    }
    return true;
}