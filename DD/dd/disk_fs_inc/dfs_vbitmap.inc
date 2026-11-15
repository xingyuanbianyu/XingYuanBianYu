// ==========================================================
// 🚀 域遍布 (Virtual Bitmap) v4
// 布局（与 dfs_super.inc 保持一致）：
//   VBITMAP_TABLE_BLOCK        : VirtualBitmapHeader (16B)
//   VBITMAP_TABLE_BLOCK + 1..N : VirtualBitmapNode 数组
// 严格约定：节点区共 (VBITMAP_TABLE_BLOCKS_DEF - 1) 块
//           即最多 VBITMAP_MAX_NODES = 3 * 256 = 768 个域
//
// ⚠️ 核心原则：node_index 必须严格等于 domain_id，永远不压缩数组！
// ==========================================================

// 🚀 注意：VBITMAP_TABLE_BLOCK / VBITMAP_TABLE_BLOCKS_DEF / VBITMAP_MAX_NODES
// 已在 disk_fs.h 中全局定义，不要重复定义！

// ---------- 内部 helper：不再触发外部锁 ----------
static bool vbitmap_hdr_read_unlocked(BlockDevice* disk, VirtualBitmapHeader& hdr) {
    if (!disk) return false;
    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(VBITMAP_TABLE_BLOCK, buf)) return false;
    std::memcpy(&hdr, buf, sizeof(hdr));
    return hdr.magic == VBITMAP_MAGIC && hdr.node_count <= VBITMAP_MAX_NODES;
}

static bool vbitmap_hdr_write_unlocked(BlockDevice* disk, const VirtualBitmapHeader& hdr) {
    if (!disk) return false;
    uint8_t buf[BLOCK_SIZE] = {0};
    std::memcpy(buf, &hdr, sizeof(hdr));
    return disk->write_sector(VBITMAP_TABLE_BLOCK, buf);
}

// 🚀 局部读取 DomainEntry（不走 domain_read 加锁路径）
static bool vbitmap_read_domain_unlocked(BlockDevice* disk, uint32_t domain_id, DomainEntry& out) {
    Superblock sb{};
    if (!read_super(disk, sb)) return false;                // dfs_super.inc 提供
    if (domain_id >= sb.domain_count) return false;

    uint32_t entries_per_block = DOMAIN_PER_BLOCK;
    uint32_t blk  = sb.domain_table_start + 1 + domain_id / entries_per_block;
    uint32_t slot = domain_id % entries_per_block;

    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(blk, buf)) return false;
    out = reinterpret_cast<DomainEntry*>(buf)[slot];
    return true; // 🚀 即使 domain_id 是空槽，也返回 true，让上层判断 flags
}

// 🚀 逐块读取所有节点（同块只读一次）
static bool vbitmap_read_all_unlocked(BlockDevice* disk,
                                      VirtualBitmapHeader& hdr,
                                      std::vector<VirtualBitmapNode>& out) {
    if (!vbitmap_hdr_read_unlocked(disk, hdr)) return false;
    out.clear();
    out.reserve(hdr.node_count);

    uint32_t blk_count = (hdr.node_count + VBITMAP_PER_BLOCK - 1) / VBITMAP_PER_BLOCK;
    for (uint32_t b = 0; b < blk_count; ++b) {
        uint8_t buf[BLOCK_SIZE] = {0};
        if (!disk->read_sector(VBITMAP_TABLE_BLOCK + 1 + b, buf)) return false;
        VirtualBitmapNode* arr = reinterpret_cast<VirtualBitmapNode*>(buf);
        uint32_t remain = hdr.node_count - out.size();
        uint32_t take = remain < VBITMAP_PER_BLOCK ? remain : VBITMAP_PER_BLOCK;
        for (uint32_t k = 0; k < take; ++k) out.push_back(arr[k]);
    }
    return true;
}

// ==========================================================
// 公开 API
// ==========================================================
bool DiskFileSystem::vbitmap_append(BlockDevice* disk, uint32_t domain_id) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::unique_lock<std::shared_mutex> lk(g_fs_rwlock);

    // 🚀 直接复用 domain 层的同步逻辑，保证绝对一致
    DomainEntry de{};
    if (!vbitmap_read_domain_unlocked(disk, domain_id, de)) return false;
    uint32_t flags = (de.domain_id == DOMAIN_SLOT_FREE) ? 0 : 1;
    
    // 修改对应索引的节点（不是追加）
    return domain_vbitmap_sync_unlocked(disk, domain_id, de.inode_start, flags);
}

bool DiskFileSystem::vbitmap_remove(BlockDevice* disk, uint32_t domain_id) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::unique_lock<std::shared_mutex> lk(g_fs_rwlock);

    // 🚀 核心修复：直接原地清空 flags，绝不压缩数组！
    return domain_vbitmap_sync_unlocked(disk, domain_id, 0, 0);
}

bool DiskFileSystem::vbitmap_read(BlockDevice* disk, VirtualBitmapHeader& hdr,
                                  std::vector<VirtualBitmapNode>& out_nodes) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::shared_lock<std::shared_mutex> lk(g_fs_rwlock);
    return vbitmap_read_all_unlocked(disk, hdr, out_nodes);
}

bool DiskFileSystem::vbitmap_rebuild(BlockDevice* disk) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::unique_lock<std::shared_mutex> lk(g_fs_rwlock);

    // 读超级块
    Superblock sb{};
    if (!read_super(disk, sb)) return false;
    if (sb.domain_count > VBITMAP_MAX_NODES) {
        std::cerr << "[VBITMAP] 域数量超出虚拟位图容量，拒绝重建\n";
        return false;
    }

    // 记录旧的 node_count，用于"清掉多余尾部"
    VirtualBitmapHeader old_hdr{};
    uint32_t old_count = vbitmap_hdr_read_unlocked(disk, old_hdr) ? old_hdr.node_count : 0;

    // 生成新节点（高水位 = domain_count）
    VirtualBitmapHeader hdr{};
    hdr.magic      = VBITMAP_MAGIC;
    hdr.node_count = sb.domain_count; 
    hdr.next_block = 0;
    hdr.checksum   = 0;

    // 🚀 核心修复：必须严格按 id 位置写入，不能压缩！
    for (uint32_t id = 0; id < sb.domain_count; ++id) {
        DomainEntry de{};
        vbitmap_read_domain_unlocked(disk, id, de); // 读失败默认全 0

        uint32_t flags = (de.domain_id == DOMAIN_SLOT_FREE) ? 0 : 1;
        VirtualBitmapNode n{ id, id, (flags ? de.inode_start : 0), flags };

        uint32_t blk = VBITMAP_TABLE_BLOCK + 1 + id / VBITMAP_PER_BLOCK;
        uint32_t off = (id % VBITMAP_PER_BLOCK) * sizeof(VirtualBitmapNode);
        uint8_t buf[BLOCK_SIZE] = {0};
        if (!disk->read_sector(blk, buf)) std::memset(buf, 0, BLOCK_SIZE);

        std::memcpy(buf + off, &n, sizeof(n));
        disk->write_sector(blk, buf);

        hdr.checksum ^= simple_checksum(reinterpret_cast<const uint8_t*>(&n), sizeof(n));
    }

    // 🚀 精确清掉 [新数量, 旧数量) 的残留槽位
    if (old_count > hdr.node_count) {
        for (uint32_t i = hdr.node_count; i < old_count; ++i) {
            uint32_t blk = VBITMAP_TABLE_BLOCK + 1 + i / VBITMAP_PER_BLOCK;
            uint32_t off = (i % VBITMAP_PER_BLOCK) * sizeof(VirtualBitmapNode);
            uint8_t buf[BLOCK_SIZE] = {0};
            if (!disk->read_sector(blk, buf)) continue;
            std::memset(buf + off, 0, sizeof(VirtualBitmapNode));
            disk->write_sector(blk, buf);
        }
    }

    return vbitmap_hdr_write_unlocked(disk, hdr);
}