// ============================================================
// dfs_inode.inc —— inode 表读写与分配（per-disk，加固版 v2）
//
// 锁策略：
//   - inode_read_unlocked / inode_write_unlocked : **不加锁**，调用者负责
//   - inode_read / inode_write                   : 加 shared / unique lock
//   - inode_alloc / inode_free                   : 内部 unique lock + _unlocked helper
//
// 数据块释放策略：
//   - 文件 (type=0) : start_block 链尾 FILE_DATA_CHUNK 处为 next，全清；
//                     indirect_block 若存在则释放其 IndexBlock 里所有 pointers
//   - 目录 (type=1) : 通过 __NEXT__ 标记遍历，全清；ext_block 视为单块（保守）
//   - 链接/作用块   : 直接释放 start_block 与 ext_block（单块，保守）
// ============================================================

inline constexpr uint32_t INODE_ROOT_ID = 2;

// ---------- 内部无锁 helper ----------

static bool inode_read_unlocked(BlockDevice* disk, uint32_t inode_id, InodeEntry& out) {
    if (!disk) return false;

    Superblock sb{};
    if (!read_super(disk, sb)) return false;
    if (inode_id >= sb.inode_count) return false;

    // 🚀 块号硬上限：防止 SB 脏数据导致越界
    uint32_t max_inode = sb.inode_table_blocks * INODE_PER_BLOCK;
    if (inode_id >= max_inode) return false;

    uint32_t blk  = sb.inode_table_start + inode_id / INODE_PER_BLOCK;
    uint32_t slot = inode_id % INODE_PER_BLOCK;

    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(blk, buf)) return false;

    out = reinterpret_cast<const InodeEntry*>(buf)[slot];
    return true;
}

static bool inode_write_unlocked(BlockDevice* disk, uint32_t inode_id, const InodeEntry& in) {
    if (!disk) return false;

    Superblock sb{};
    if (!read_super(disk, sb)) return false;
    if (inode_id >= sb.inode_count) return false;

    uint32_t max_inode = sb.inode_table_blocks * INODE_PER_BLOCK;
    if (inode_id >= max_inode) return false;

    uint32_t blk  = sb.inode_table_start + inode_id / INODE_PER_BLOCK;
    uint32_t slot = inode_id % INODE_PER_BLOCK;

    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(blk, buf)) return false;

    InodeEntry entry = in;
    entry.inode_id = inode_id;                       // 🚀 强制纠正防止槽位错位
    std::memset(entry.reserved, 0, sizeof(entry.reserved));   // 🚀 清 reserved

    reinterpret_cast<InodeEntry*>(buf)[slot] = entry;
    return disk->write_sector(blk, buf);
}

// 🚀 完整释放 inode 持有的数据链（文件 + 目录）
static void free_inode_data_chain_unlocked(BlockDevice* disk, const InodeEntry& ie) {
    if (!disk) return;
    uint32_t total = total_blocks_of(disk);

    // ---- 1. 数据链（start_block）----
    uint32_t b = ie.start_block;
    std::set<uint32_t> visited;
    while (b != 0 && b < total && !visited.count(b)) {
        visited.insert(b);

        uint32_t next = 0;
        uint8_t  buf[BLOCK_SIZE] = {0};
        if (disk->read_sector(b, buf)) {
            if (ie.type == 0) {
                // 文件：块尾 4 字节是 next_block
                std::memcpy(&next, buf + FILE_DATA_CHUNK, 4);
            } else {
                // 目录/其他：通过 __NEXT__ 标记跳转
                size_t off = 0;
                while (off + DIR_ENTRY_HEADER_SIZE <= BLOCK_SIZE) {
                    DirEntryHeader* hdr = reinterpret_cast<DirEntryHeader*>(buf + off);
                    if (off + DIR_ENTRY_HEADER_SIZE + hdr->name_len > BLOCK_SIZE) break;

                    if (hdr->inode_id == 0) {
                        off += DIR_ENTRY_HEADER_SIZE + hdr->name_len;
                        continue;
                    }
                    std::string nm(reinterpret_cast<const char*>(buf + off + DIR_ENTRY_HEADER_SIZE),
                                   hdr->name_len);
                    if (nm == "__NEXT__") {
                        next = hdr->start_sector;
                        break;
                    }
                    off += DIR_ENTRY_HEADER_SIZE + hdr->name_len;
                }
            }
        }
        DiskFileSystem::free_sector(disk, b);
        b = next;
    }

    // ---- 2. 间接块（仅文件）----
    if (ie.type == 0 && ie.indirect_block != 0 && ie.indirect_block < total) {
        uint8_t ibuf[BLOCK_SIZE] = {0};
        if (disk->read_sector(ie.indirect_block, ibuf)) {
            const IndexBlock* ib = reinterpret_cast<const IndexBlock*>(ibuf);
            if (ib->magic == INDEX_MAGIC) {
                uint32_t cnt = ib->count;
                if (cnt > INDEX_PTR_MAX) cnt = INDEX_PTR_MAX;
                for (uint32_t k = 0; k < cnt; ++k) {
                    uint32_t p = ib->pointers[k];
                    if (p != 0 && p < total) DiskFileSystem::free_sector(disk, p);
                }
            }
        }
        DiskFileSystem::free_sector(disk, ie.indirect_block);
    }

    // ---- 3. 扩展块（目录/链接/作用块；保守视为单块）----
    if (ie.type != 0 && ie.ext_block != 0 && ie.ext_block < total) {
        DiskFileSystem::free_sector(disk, ie.ext_block);
    }
}

// ---------- 公开 API ----------

bool DiskFileSystem::inode_read(BlockDevice* disk, uint32_t inode_id, InodeEntry& out) {
    if (!disk) return false;
    DiskScope scope(disk);
    std::shared_lock<std::shared_mutex> lock(g_fs_rwlock);
    return inode_read_unlocked(disk, inode_id, out);
}

bool DiskFileSystem::inode_write(BlockDevice* disk, uint32_t inode_id, const InodeEntry& in) {
    if (!disk) return false;
    DiskScope scope(disk);
    // 🚀 RMW 整块必须 exclusive：两个线程改同块不同 slot，shared 会 lost update
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);
    return inode_write_unlocked(disk, inode_id, in);
}

uint32_t DiskFileSystem::inode_alloc(BlockDevice* disk, uint32_t type,
                                      uint32_t parent_inode, uint32_t domain_id) {
    if (!disk) return 0;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);

    Superblock sb{};
    if (!read_super(disk, sb)) return 0;

    // 🚀 位图必须已初始化（mount 后 rebuild_bitmap 完成）
    auto& bm = inode_bitmap_of(disk);
    if (bm.empty()) {
        std::cerr << "[INODE_ALLOC] inode 位图未初始化，拒绝分配\n";
        return 0;
    }

    for (uint32_t i = INODE_ROOT_ID; i < sb.inode_count; ++i) {
        if (inode_bitmap_test(disk, i)) continue;

        // 🚀 找到空槽：先占位图（幂等），再写 inode
        inode_bitmap_set(disk, i, true);

        InodeEntry ie{};
        ie.inode_id     = i;
        ie.type         = type;
        ie.size         = 0;
        ie.start_block  = 0;
        ie.ext_block    = 0;
        ie.block_count  = 0;
        ie.parent_inode = parent_inode;
        ie.domain_id    = domain_id;
        ie.link_count   = 1;
        ie.owner_id     = 0;
        std::memset(ie.resource_tag, 0, sizeof(ie.resource_tag));

        if (!inode_write_unlocked(disk, i, ie)) {
            inode_bitmap_set(disk, i, false);
            return 0;
        }
        return i;
    }
    return 0;
}

bool DiskFileSystem::inode_free(BlockDevice* disk, uint32_t inode_id) {
    if (!disk || inode_id < INODE_ROOT_ID) return false;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);

    // 🚀 幂等：位图已空 → 视为已释放，直接返回成功
    if (!inode_bitmap_test(disk, inode_id)) return true;

    InodeEntry ie{};
    if (!inode_read_unlocked(disk, inode_id, ie)) {
        // 读不到 InodeEntry，但位图说它被占用 → 保守清位图
        inode_bitmap_set(disk, inode_id, false);
        return true;
    }

    // 🚀 完整释放数据链（文件 + 目录 + 间接块 + 扩展块）
    free_inode_data_chain_unlocked(disk, ie);

    // 🚀 清空 InodeEntry（避免下次 inode_read 读到残留 id）
    InodeEntry empty{};
    inode_write_unlocked(disk, inode_id, empty);

    // 最后清位图
    inode_bitmap_set(disk, inode_id, false);
    return true;
}