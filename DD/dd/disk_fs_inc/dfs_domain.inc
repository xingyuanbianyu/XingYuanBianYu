// ============================================================
// dfs_domain.inc —— 域表读写（per-disk，domain ↔ vbitmap 强一致）
//
// 关键约定：
//   1. vbitmap.node[i] 与 domain[i] 按**位置**一一对应
//      - node_index = i
//      - domain_id  = i（永久保留，即使 domain 被释放）
//      - inode_start 与 domain.inode_start 同步（空槽时 = 0）
//      - flags: 0=free, 1=used
//   2. vbitmap.node_count 等于 domain_count（高水位，只增不减）
//   3. 每次 domain_alloc / domain_free 都同步刷新 vbitmap，
//      因此 tree_map_check 的 (数量一致 + 位置一致 + inode_start 一致 + checksum 一致) 恒成立
// ============================================================

// 🚀 注意：DOMAIN_SLOT_FREE 已在 disk_fs.h 中全局定义
// inline constexpr uint32_t DOMAIN_SLOT_FREE = 0xFFFFFFFFu;

inline constexpr uint32_t DOMAIN_NAME_CAP  = 32; // 与 DomainEntry::name[32] 对齐

// ---------- 内部 helper ----------

// 名字比较：以 null 分隔，长度严格比对
static bool domain_name_equal(const char* a, const char* b, uint32_t cap) {
    if (!a || !b) return false;
    uint32_t la = strnlen(a, cap);
    uint32_t lb = strnlen(b, cap);
    if (la != lb) return false;
    return std::memcmp(a, b, la) == 0;
}

// 同步 vbitmap 中 domain_id 对应的 node（无锁，供 alloc/free 在 unique_lock 内调用）
static bool domain_vbitmap_sync_unlocked(BlockDevice* disk, uint32_t domain_id,
                                          uint32_t inode_start, uint32_t flags) {
    if (!disk) return false;
    if (domain_id >= VBITMAP_MAX_NODES) return false;

    // 读 header
    uint8_t hbuf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(VBITMAP_TABLE_BLOCK, hbuf)) return false;
    VirtualBitmapHeader hdr{};
    std::memcpy(&hdr, hbuf, sizeof(hdr));
    if (hdr.magic != VBITMAP_MAGIC) {
        std::memset(&hdr, 0, sizeof(hdr));
        hdr.magic = VBITMAP_MAGIC;
    }

    // 更新节点（按位置 = domain_id）
    uint32_t node_blk = VBITMAP_TABLE_BLOCK + 1 + domain_id / VBITMAP_PER_BLOCK;
    uint32_t slot     = domain_id % VBITMAP_PER_BLOCK;

    uint8_t nbuf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(node_blk, nbuf)) return false;
    VirtualBitmapNode* arr = reinterpret_cast<VirtualBitmapNode*>(nbuf);
    arr[slot].node_index  = domain_id;
    arr[slot].domain_id   = domain_id;                 // 永久保留
    arr[slot].inode_start = (flags ? inode_start : 0);
    arr[slot].flags       = flags;
    if (!disk->write_sector(node_blk, nbuf)) return false;

    // node_count 只增不减（高水位 = domain_count）
    if (hdr.node_count <= domain_id) hdr.node_count = domain_id + 1;

    // 重算 checksum（XOR 累积，与 tree_map_check 一致）
    uint32_t checksum = 0;
    uint32_t nblks = (hdr.node_count + VBITMAP_PER_BLOCK - 1) / VBITMAP_PER_BLOCK;
    for (uint32_t b = 0; b < nblks; ++b) {
        uint8_t tmp[BLOCK_SIZE] = {0};
        if (!disk->read_sector(VBITMAP_TABLE_BLOCK + 1 + b, tmp)) return false;
        VirtualBitmapNode* ar = reinterpret_cast<VirtualBitmapNode*>(tmp);
        uint32_t cnt = std::min((uint32_t)VBITMAP_PER_BLOCK,
                                hdr.node_count - b * VBITMAP_PER_BLOCK);
        for (uint32_t s = 0; s < cnt; ++s) {
            checksum ^= DiskFileSystem::simple_checksum(
                reinterpret_cast<const uint8_t*>(&ar[s]), sizeof(VirtualBitmapNode));
        }
    }
    hdr.checksum = checksum;

    std::memcpy(hbuf, &hdr, sizeof(hdr));
    return disk->write_sector(VBITMAP_TABLE_BLOCK, hbuf);
}

// ---------- 公开 API ----------

int32_t DiskFileSystem::domain_find_by_name(BlockDevice* disk, const std::string& name) {
    if (!disk) return -1;
    DiskScope scope(disk);
    std::shared_lock<std::shared_mutex> lock(g_fs_rwlock);

    Superblock sb{};
    if (!read_super(disk, sb)) return -1;
    if (name.size() >= DOMAIN_NAME_CAP) return -1;

    uint32_t blocks_needed = (sb.domain_count + DOMAIN_PER_BLOCK - 1) / DOMAIN_PER_BLOCK;
    for (uint32_t b_idx = 0; b_idx < blocks_needed; ++b_idx) {
        uint32_t blk = sb.domain_table_start + 1 + b_idx;
        uint8_t buf[BLOCK_SIZE] = {0};
        if (!disk->read_sector(blk, buf)) continue;

        DomainEntry* arr = reinterpret_cast<DomainEntry*>(buf);
        uint32_t count_in_block = std::min(
            (uint32_t)DOMAIN_PER_BLOCK,
            sb.domain_count - b_idx * DOMAIN_PER_BLOCK);

        for (uint32_t slot = 0; slot < count_in_block; ++slot) {
            if (arr[slot].domain_id == DOMAIN_SLOT_FREE) continue;
            if (domain_name_equal(arr[slot].name, name.c_str(), DOMAIN_NAME_CAP)) {
                return static_cast<int32_t>(arr[slot].domain_id);
            }
        }
    }
    return -1;
}

bool DiskFileSystem::domain_read(BlockDevice* disk, uint32_t domain_id, DomainEntry& out) {
    if (!disk) return false;
    DiskScope scope(disk);
    std::shared_lock<std::shared_mutex> lock(g_fs_rwlock);

    Superblock sb{};
    if (!read_super(disk, sb)) return false;
    if (domain_id >= sb.domain_count) return false;

    // 🚀 容量硬检查
    uint32_t max_domains = (sb.domain_table_blocks > 0)
        ? (sb.domain_table_blocks - 1) * DOMAIN_PER_BLOCK : 0;
    if (domain_id >= max_domains) return false;

    uint32_t blk  = sb.domain_table_start + 1 + domain_id / DOMAIN_PER_BLOCK;
    uint32_t slot = domain_id % DOMAIN_PER_BLOCK;

    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(blk, buf)) return false;

    DomainEntry* arr = reinterpret_cast<DomainEntry*>(buf);
    out = arr[slot];
    // 🚀 空槽允许返回：调用者须判 out.domain_id == DOMAIN_SLOT_FREE
    return true;
}

bool DiskFileSystem::domain_write(BlockDevice* disk, uint32_t domain_id,
                                   const DomainEntry& in) {
    if (!disk) return false;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);   // RMW 需排他

    Superblock sb{};
    if (!read_super(disk, sb)) return false;
    if (domain_id >= sb.domain_count) return false;

    uint32_t max_domains = (sb.domain_table_blocks > 0)
        ? (sb.domain_table_blocks - 1) * DOMAIN_PER_BLOCK : 0;
    if (domain_id >= max_domains) return false;

    uint32_t blk  = sb.domain_table_start + 1 + domain_id / DOMAIN_PER_BLOCK;
    uint32_t slot = domain_id % DOMAIN_PER_BLOCK;

    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(blk, buf)) return false;

    DomainEntry* arr = reinterpret_cast<DomainEntry*>(buf);
    DomainEntry copy = in;
    if (copy.domain_id != DOMAIN_SLOT_FREE) copy.domain_id = domain_id;  // 🚀 强制纠正
    arr[slot] = copy;

    if (!disk->write_sector(blk, buf)) return false;

    // 同步 vbitmap
    uint32_t flags = (copy.domain_id == DOMAIN_SLOT_FREE) ? 0u : 1u;
    domain_vbitmap_sync_unlocked(disk, domain_id, copy.inode_start, flags);
    return true;
}

uint32_t DiskFileSystem::domain_alloc(BlockDevice* disk, uint32_t parent_id,
                                       const std::string& name) {
    if (!disk) return DOMAIN_SLOT_FREE;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);

    if (name.empty()) {
        std::cerr << "[DOMAIN_ALLOC] 名字为空\n";
        return DOMAIN_SLOT_FREE;
    }
    if (name.size() >= DOMAIN_NAME_CAP) {
        std::cerr << "[DOMAIN_ALLOC] 名字超长（" << name.size()
                  << " >= " << DOMAIN_NAME_CAP << "）\n";
        return DOMAIN_SLOT_FREE;
    }

    Superblock sb{};
    if (!read_super(disk, sb)) return DOMAIN_SLOT_FREE;

    if (sb.domain_table_blocks <= 1) return DOMAIN_SLOT_FREE;
    uint32_t max_domains = (sb.domain_table_blocks - 1) * DOMAIN_PER_BLOCK;

    // 扫现有域：查重 + 找空槽
    uint32_t free_id = DOMAIN_SLOT_FREE;
    bool     name_exists = false;
    uint32_t blocks_needed = (sb.domain_count + DOMAIN_PER_BLOCK - 1) / DOMAIN_PER_BLOCK;

    for (uint32_t b_idx = 0; b_idx < blocks_needed && !name_exists; ++b_idx) {
        uint32_t blk = sb.domain_table_start + 1 + b_idx;
        uint8_t buf[BLOCK_SIZE] = {0};
        if (!disk->read_sector(blk, buf)) return DOMAIN_SLOT_FREE;

        DomainEntry* arr = reinterpret_cast<DomainEntry*>(buf);
        uint32_t count_in_block = std::min(
            (uint32_t)DOMAIN_PER_BLOCK,
            sb.domain_count - b_idx * DOMAIN_PER_BLOCK);

        for (uint32_t slot = 0; slot < count_in_block; ++slot) {
            if (arr[slot].domain_id == DOMAIN_SLOT_FREE) {
                if (free_id == DOMAIN_SLOT_FREE)
                    free_id = b_idx * DOMAIN_PER_BLOCK + slot;
            } else if (domain_name_equal(arr[slot].name, name.c_str(), DOMAIN_NAME_CAP)) {
                name_exists = true;
                break;
            }
        }
    }

    if (name_exists) {
        std::cerr << "[DOMAIN_ALLOC] 名字重复: " << name << "\n";
        return DOMAIN_SLOT_FREE;
    }

    // 决定 id
    bool extended = false;
    uint32_t id;
    if (free_id != DOMAIN_SLOT_FREE) {
        id = free_id;                         // 复用空槽
    } else {
        if (sb.domain_count >= max_domains) {
            std::cerr << "[DOMAIN_ALLOC] 域表已满 (" << max_domains << ")\n";
            return DOMAIN_SLOT_FREE;
        }
        id = sb.domain_count;
        sb.domain_count = id + 1;
        extended = true;
    }

    // 构造 entry
    DomainEntry de{};
    de.domain_id   = id;
    de.parent_id   = parent_id;
    de.inode_start = sb.inode_table_start;
    de.inode_count = sb.inode_count;
    de.data_start  = sb.data_start;
    uint32_t total_b = total_blocks_of(disk);
    de.data_end = (total_b > RESERVED_BLOCKS) ? (total_b - RESERVED_BLOCKS) : total_b;
    strncpy(de.name, name.c_str(), DOMAIN_NAME_CAP - 1);
    de.name[DOMAIN_NAME_CAP - 1] = '\0';

    // 写 entry
    uint32_t blk  = sb.domain_table_start + 1 + id / DOMAIN_PER_BLOCK;
    uint32_t slot = id % DOMAIN_PER_BLOCK;

    uint8_t wbuf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(blk, wbuf)) return DOMAIN_SLOT_FREE;
    DomainEntry* warr = reinterpret_cast<DomainEntry*>(wbuf);
    warr[slot] = de;
    if (!disk->write_sector(blk, wbuf)) return DOMAIN_SLOT_FREE;

    // 🚀 同步 vbitmap（domain ↔ vbitmap 强一致）
    if (!domain_vbitmap_sync_unlocked(disk, id, de.inode_start, 1)) {
        // 回滚 entry
        std::memset(&warr[slot], 0, sizeof(DomainEntry));
        warr[slot].domain_id = DOMAIN_SLOT_FREE;
        disk->write_sector(blk, wbuf);
        return DOMAIN_SLOT_FREE;
    }

    // 扩展高水位时才更新超块
    if (extended) {
        if (!write_super(disk, sb)) {
            // 回滚 entry + vbitmap
            std::memset(&warr[slot], 0, sizeof(DomainEntry));
            warr[slot].domain_id = DOMAIN_SLOT_FREE;
            disk->write_sector(blk, wbuf);
            domain_vbitmap_sync_unlocked(disk, id, 0, 0);
            return DOMAIN_SLOT_FREE;
        }
    }
    return id;
}

bool DiskFileSystem::domain_free(BlockDevice* disk, uint32_t domain_id) {
    if (!disk || domain_id == 0) return false;   // 根域不可释放
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);

    Superblock sb{};
    if (!read_super(disk, sb)) return false;
    if (domain_id >= sb.domain_count) return false;

    uint32_t max_domains = (sb.domain_table_blocks > 0)
        ? (sb.domain_table_blocks - 1) * DOMAIN_PER_BLOCK : 0;
    if (domain_id >= max_domains) return false;

    uint32_t blk  = sb.domain_table_start + 1 + domain_id / DOMAIN_PER_BLOCK;
    uint32_t slot = domain_id % DOMAIN_PER_BLOCK;

    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(blk, buf)) return false;

    DomainEntry* arr = reinterpret_cast<DomainEntry*>(buf);
    if (arr[slot].domain_id == DOMAIN_SLOT_FREE) {
        // 幂等：仍同步一次 vbitmap（防脏数据）
        domain_vbitmap_sync_unlocked(disk, domain_id, 0, 0);
        return true;
    }

    // 清空 entry
    std::memset(&arr[slot], 0, sizeof(DomainEntry));
    arr[slot].domain_id = DOMAIN_SLOT_FREE;
    if (!disk->write_sector(blk, buf)) return false;

    // 🚀 同步 vbitmap（flags=0）
    domain_vbitmap_sync_unlocked(disk, domain_id, 0, 0);
    return true;
}