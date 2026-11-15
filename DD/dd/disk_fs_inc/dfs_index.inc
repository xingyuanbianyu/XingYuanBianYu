// ============================================================
// dfs_index.inc —— 索引块（异块）读写（per-disk，加固版 v2）
//
// 锁策略：
//   - index_*_unlocked : 不加锁，调用者负责（供已持锁的 .inc 使用）
//   - index_read       : shared_lock
//   - index_write      : unique_lock（RMW 整块覆盖）
//   - index_alloc      : unique_lock（allocation + 落盘 原子化）
//   - index_free       : unique_lock（magic 校验 + 位图一致）
//
// 语义约定：
//   - index_free 只释放**索引块本身**；
//     若需连带释放 pointers 指向的数据块，调用 index_free_with_data()
//   - 数据块链释放由 inode_free / free_inode_data_chain_unlocked 负责
// ============================================================

// ---------- 内部无锁 helper ----------

static bool index_read_unlocked(BlockDevice* disk, uint32_t index_block, IndexBlock& out) {
    if (!disk || index_block == 0) return false;
    if (!is_block_valid(disk, index_block)) return false;

    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(index_block, buf)) return false;

    const IndexBlock* ib = reinterpret_cast<const IndexBlock*>(buf);
    if (ib->magic != INDEX_MAGIC) return false;
    if (ib->count > INDEX_PTR_MAX) return false;          // 🚀 防脏数据

    out = *ib;
    return true;
}

static bool index_write_unlocked(BlockDevice* disk, uint32_t index_block,
                                 const IndexBlock& in) {
    if (!disk || index_block == 0) return false;
    if (!is_block_valid(disk, index_block)) return false;

    // 🚀 拒写脏数据：magic / count 必检
    if (in.magic != INDEX_MAGIC) {
        std::cerr << "[INDEX_WRITE] magic 不匹配，拒绝写入\n";
        return false;
    }
    if (in.count > INDEX_PTR_MAX) {
        std::cerr << "[INDEX_WRITE] count(" << in.count
                  << ") 超过上限 " << INDEX_PTR_MAX << "，拒绝写入\n";
        return false;
    }

    static_assert(sizeof(IndexBlock) <= BLOCK_SIZE, "IndexBlock 超出块大小");

    uint8_t buf[BLOCK_SIZE] = {0};
    std::memcpy(buf, &in, sizeof(IndexBlock));
    return disk->write_sector(index_block, buf);
}

static uint32_t index_alloc_unlocked(BlockDevice* disk) {
    if (!disk) return 0;

    uint32_t b = DiskFileSystem::allocate_sector(disk);
    if (b == 0) return 0;

    IndexBlock ib{};
    ib.magic = INDEX_MAGIC;
    ib.count = 0;

    uint8_t buf[BLOCK_SIZE] = {0};
    std::memcpy(buf, &ib, sizeof(IndexBlock));

    if (!disk->write_sector(b, buf)) {
        DiskFileSystem::free_sector(disk, b);
        return 0;
    }
    return b;
}

// 只释放索引块本身（magic 校验防止误释放非索引块）
static bool index_free_unlocked(BlockDevice* disk, uint32_t index_block) {
    if (!disk || index_block == 0) return false;
    if (!is_block_valid(disk, index_block)) return false;

    // 🚀 校验 magic：不是 IndexBlock 就拒绝
    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(index_block, buf)) return false;
    const IndexBlock* ib = reinterpret_cast<const IndexBlock*>(buf);
    if (ib->magic != INDEX_MAGIC) {
        std::cerr << "[INDEX_FREE] 块 " << index_block
                  << " 不是 IndexBlock（magic 不符），拒绝释放\n";
        return false;
    }

    DiskFileSystem::free_sector(disk, index_block);
    return true;
}

// ---------- 公开 API ----------

bool DiskFileSystem::index_read(BlockDevice* disk, uint32_t index_block, IndexBlock& out) {
    if (!disk) return false;
    DiskScope scope(disk);
    std::shared_lock<std::shared_mutex> lock(g_fs_rwlock);
    return index_read_unlocked(disk, index_block, out);
}

bool DiskFileSystem::index_write(BlockDevice* disk, uint32_t index_block,
                                 const IndexBlock& in) {
    if (!disk) return false;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);   // 🚀 保护整块 RMW
    return index_write_unlocked(disk, index_block, in);
}

uint32_t DiskFileSystem::index_alloc(BlockDevice* disk) {
    if (!disk) return 0;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);   // 🚀 阻止并发 rebuild
    return index_alloc_unlocked(disk);
}

void DiskFileSystem::index_free(BlockDevice* disk, uint32_t index_block) {
    if (!disk) return;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);
    index_free_unlocked(disk, index_block);
}

// ============================================================
// 🚀 可选扩展：连带释放 pointers 指向的数据块
// 供 inode_free 之外的场景使用（例如直接操作索引块）
// 语义：先把 [0, count) 内所有非零 pointers 释放，再释放索引块本身
// ============================================================
bool DiskFileSystem::index_free_with_data(BlockDevice* disk, uint32_t index_block) {
    if (!disk || index_block == 0) return false;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);

    if (!is_block_valid(disk, index_block)) return false;

    // 读 IndexBlock
    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(index_block, buf)) return false;
    const IndexBlock* ib = reinterpret_cast<const IndexBlock*>(buf);
    if (ib->magic != INDEX_MAGIC) return false;
    if (ib->count > INDEX_PTR_MAX) return false;

    uint32_t total = total_blocks_of(disk);

    // 释放所有 pointers（去重防 double-free）
    std::set<uint32_t> freed;
    for (uint32_t k = 0; k < ib->count; ++k) {
        uint32_t p = ib->pointers[k];
        if (p == 0 || p == index_block) continue;
        if (p >= total) continue;
        if (freed.count(p)) continue;
        freed.insert(p);
        free_sector(disk, p);
    }

    // 释放索引块本身
    free_sector(disk, index_block);
    return true;
}