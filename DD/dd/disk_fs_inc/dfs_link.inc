// ============================================================
// dfs_link.inc —— 连接表读写 v4（适配 150 字节长名字版）
// 新布局（disk_fs.h）：
//   struct LinkEntryDisk {
//       uint32_t link_id;         // 0..3
//       uint32_t parent_inode;    // 4..7
//       uint32_t child_inode;     // 8..11
//       uint32_t domain_id;       // 12..15
//       uint32_t flags;           // 16..19
//       uint16_t name_len;        // 20..21
//       char     name[128];       // 22..149  ← 🚀 支持长文件名
//   };  150 字节
//
// 空槽标记：link_id == LINK_SLOT_FREE (0xFFFFFFFF)
// 名字以 name_len 为准，name 不保证 '\0' 结尾
// ============================================================

// 🚀 注意：LINK_SLOT_FREE 和 LINK_NAME_CAP 已在 disk_fs.h 中全局定义
// inline constexpr uint32_t LINK_SLOT_FREE = 0xFFFFFFFFu;
// inline constexpr uint32_t LINK_NAME_CAP  = 128;

// ---------- 内部无锁 helper ----------

static bool link_read_unlocked(BlockDevice* disk, uint32_t link_id, LinkEntry& out) {
    if (!disk) return false;

    Superblock sb{};
    if (!read_super(disk, sb)) return false;
    if (link_id >= sb.link_count) return false;

    uint32_t max_links = sb.link_table_blocks * LINKS_PER_BLOCK;
    if (link_id >= max_links) return false;                       // 🚀 容量硬检查

    uint32_t blk  = sb.link_table_start + link_id / LINKS_PER_BLOCK;
    uint32_t slot = link_id % LINKS_PER_BLOCK;

    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(blk, buf)) return false;

    const LinkEntryDisk* arr = reinterpret_cast<const LinkEntryDisk*>(buf);
    const LinkEntryDisk& d = arr[slot];
    if (d.link_id == LINK_SLOT_FREE) return false;

    // 🚀 关键修复：槽位里的 link_id 必须严格等于请求的 link_id
    if (d.link_id != link_id) {
        std::cerr << "[LINK_READ] 槽位 " << slot
                  << " 中 link_id=" << d.link_id
                  << " 与请求 id=" << link_id << " 不一致，数据损坏\n";
        return false;
    }

    // 🚀 上层防脏数据：parent/child 必须落在 inode 范围内
    if (d.parent_inode < 2 || d.parent_inode >= sb.inode_count) return false;
    if (d.child_inode  < 2 || d.child_inode  >= sb.inode_count) return false;

    out.link_id      = d.link_id;
    out.parent_inode = d.parent_inode;
    out.child_inode  = d.child_inode;
    out.domain_id    = d.domain_id;
    out.flags        = d.flags;

    // 🚀 严格按 name_len 提取，绝不越界
    uint32_t n = d.name_len;
    if (n > LINK_NAME_CAP) n = LINK_NAME_CAP;                     // 兜底防脏数据
    out.name.assign(d.name, n);
    return true;
}

static bool link_write_unlocked(BlockDevice* disk, uint32_t link_id, const LinkEntry& in) {
    if (!disk) return false;

    // 🚀 名字超长拒绝（不静默截断）
    if (in.name.size() > LINK_NAME_CAP) {
        std::cerr << "[LINK_WRITE] 名字超长（" << in.name.size()
                  << " > " << LINK_NAME_CAP << "），拒绝写入\n";
        return false;
    }

    Superblock sb{};
    if (!read_super(disk, sb)) return false;
    if (link_id >= sb.link_count) return false;

    uint32_t max_links = sb.link_table_blocks * LINKS_PER_BLOCK;
    if (link_id >= max_links) return false;

    uint32_t blk  = sb.link_table_start + link_id / LINKS_PER_BLOCK;
    uint32_t slot = link_id % LINKS_PER_BLOCK;

    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(blk, buf)) return false;

    LinkEntryDisk* arr = reinterpret_cast<LinkEntryDisk*>(buf);
    LinkEntryDisk& d = arr[slot];

    // 🚀 关键修复：若槽位已被其他 id 占用 → 拒绝（防止覆盖别人）
    if (d.link_id != LINK_SLOT_FREE && d.link_id != link_id) {
        std::cerr << "[LINK_WRITE] 槽位 " << slot
                  << " 已被 link_id=" << d.link_id
                  << " 占用，拒绝写入 id=" << link_id << "\n";
        return false;
    }

    d.link_id      = link_id;
    d.parent_inode = in.parent_inode;
    d.child_inode  = in.child_inode;
    d.domain_id    = in.domain_id;
    d.flags        = in.flags;

    // 🚀 严格按 name_len 写入，并用新容量 128 抹零
    uint32_t n = (uint32_t)in.name.size();
    d.name_len = (uint16_t)n;
    std::memset(d.name, 0, LINK_NAME_CAP);
    if (n > 0) std::memcpy(d.name, in.name.data(), n);

    return disk->write_sector(blk, buf);
}

// ---------- 公开 API ----------

bool DiskFileSystem::link_read(BlockDevice* disk, uint32_t link_id, LinkEntry& out) {
    if (!disk) return false;
    DiskScope scope(disk);
    std::shared_lock<std::shared_mutex> lock(g_fs_rwlock);
    return link_read_unlocked(disk, link_id, out);
}

bool DiskFileSystem::link_write(BlockDevice* disk, uint32_t link_id, const LinkEntry& in) {
    if (!disk) return false;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);
    return link_write_unlocked(disk, link_id, in);
}

// ---------- link_alloc ----------

uint32_t DiskFileSystem::link_alloc(BlockDevice* disk, uint32_t parent_inode,
                                     uint32_t child_inode, uint32_t domain_id,
                                     const std::string& name) {
    if (!disk) return 0;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);

    // 🚀 名字超长拒绝
    if (name.size() > LINK_NAME_CAP) {
        std::cerr << "[LINK_ALLOC] 名字超长（" << name.size()
                  << " > " << LINK_NAME_CAP << "），拒绝分配\n";
        return 0;
    }

    Superblock sb{};
    if (!read_super(disk, sb)) return 0;

    // 🚀 关键修复：parent/child inode 必须落在有效范围
    if (parent_inode < 2 || parent_inode >= sb.inode_count) {
        std::cerr << "[LINK_ALLOC] parent_inode=" << parent_inode << " 非法\n";
        return 0;
    }
    if (child_inode < 2 || child_inode >= sb.inode_count) {
        std::cerr << "[LINK_ALLOC] child_inode=" << child_inode << " 非法\n";
        return 0;
    }

    uint32_t max_links = sb.link_table_blocks * LINKS_PER_BLOCK;
    if (max_links == 0) {
        std::cerr << "[LINK_ALLOC] 链接表未初始化（link_table_blocks=0）\n";
        return 0;
    }

    // === 第一步：扫描 link_count 范围内的空槽 ===
    uint32_t free_id = LINK_SLOT_FREE;
    uint32_t used_blocks = (sb.link_count + LINKS_PER_BLOCK - 1) / LINKS_PER_BLOCK;
    for (uint32_t b_idx = 0; b_idx < used_blocks && free_id == LINK_SLOT_FREE; ++b_idx) {
        uint32_t blk = sb.link_table_start + b_idx;
        uint8_t buf[BLOCK_SIZE] = {0};
        if (!disk->read_sector(blk, buf)) return 0;

        const LinkEntryDisk* arr = reinterpret_cast<const LinkEntryDisk*>(buf);
        uint32_t remain = sb.link_count - b_idx * LINKS_PER_BLOCK;
        uint32_t count  = remain < (uint32_t)LINKS_PER_BLOCK ? remain : (uint32_t)LINKS_PER_BLOCK;
        for (uint32_t s = 0; s < count; ++s) {
            if (arr[s].link_id == LINK_SLOT_FREE) {
                free_id = b_idx * LINKS_PER_BLOCK + s;
                break;
            }
        }
    }

    // === 第二步：决定 id ===
    bool extended = false;
    uint32_t id;
    if (free_id != LINK_SLOT_FREE) {
        id = free_id;                              // 复用空槽，sb.link_count 不变
    } else {
        // 🚀 追加前必须检查容量
        if (sb.link_count >= max_links) {
            std::cerr << "[LINK_ALLOC] 链接表已满（" << max_links << "）\n";
            return 0;
        }
        id = sb.link_count;
        sb.link_count = id + 1;
        extended = true;
    }

    // === 第三步：构造并写入槽 ===
    uint32_t blk  = sb.link_table_start + id / LINKS_PER_BLOCK;
    uint32_t slot = id % LINKS_PER_BLOCK;

    uint8_t wbuf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(blk, wbuf)) return 0;

    LinkEntryDisk* warr = reinterpret_cast<LinkEntryDisk*>(wbuf);
    LinkEntryDisk& d = warr[slot];

    d.link_id      = id;
    d.parent_inode = parent_inode;
    d.child_inode  = child_inode;
    d.domain_id    = domain_id;
    d.flags        = 0;
    d.name_len     = (uint16_t)name.size();
    std::memset(d.name, 0, LINK_NAME_CAP);
    if (!name.empty()) std::memcpy(d.name, name.data(), name.size());

    if (!disk->write_sector(blk, wbuf)) return 0;

    // 🚀 仅在新增槽位时才更新超块
    if (extended) {
        if (!write_super(disk, sb)) {
            // 回滚：把刚写的槽标为空
            d.link_id = LINK_SLOT_FREE;
            std::memset(d.name, 0, LINK_NAME_CAP);
            d.name_len = 0;
            disk->write_sector(blk, wbuf);
            return 0;
        }
    }
    return id;
}

// ---------- link_free ----------

bool DiskFileSystem::link_free(BlockDevice* disk, uint32_t link_id) {
    if (!disk) return false;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);

    Superblock sb{};
    if (!read_super(disk, sb)) return false;
    if (link_id >= sb.link_count) return false;

    uint32_t max_links = sb.link_table_blocks * LINKS_PER_BLOCK;
    if (link_id >= max_links) return false;

    uint32_t blk  = sb.link_table_start + link_id / LINKS_PER_BLOCK;
    uint32_t slot = link_id % LINKS_PER_BLOCK;

    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(blk, buf)) return false;

    LinkEntryDisk* arr = reinterpret_cast<LinkEntryDisk*>(buf);
    if (arr[slot].link_id == LINK_SLOT_FREE) return true;         // 幂等
    if (arr[slot].link_id != link_id) {
        // 🚀 关键修复：槽里存的是别人的 id，拒绝误删
        std::cerr << "[LINK_FREE] 槽位 " << slot
                  << " 中 link_id=" << arr[slot].link_id
                  << " 与请求 id=" << link_id << " 不一致，拒绝误删\n";
        return false;
    }

    std::memset(&arr[slot], 0, sizeof(LinkEntryDisk));
    arr[slot].link_id = LINK_SLOT_FREE;
    return disk->write_sector(blk, buf);
}

// ---------- link_free_by_nodes ----------

bool DiskFileSystem::link_free_by_nodes(BlockDevice* disk,
                                         uint32_t parent_inode, uint32_t child_inode) {
    if (!disk) return false;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);

    Superblock sb{};
    if (!read_super(disk, sb)) return false;

    uint32_t max_links = sb.link_table_blocks * LINKS_PER_BLOCK;
    uint32_t limit     = std::min(sb.link_count, max_links);

    std::vector<uint32_t> to_free;
    uint32_t blocks = (limit + LINKS_PER_BLOCK - 1) / LINKS_PER_BLOCK;

    for (uint32_t b_idx = 0; b_idx < blocks; ++b_idx) {
        uint32_t blk = sb.link_table_start + b_idx;
        uint8_t buf[BLOCK_SIZE] = {0};
        if (!disk->read_sector(blk, buf)) return false;

        const LinkEntryDisk* arr = reinterpret_cast<const LinkEntryDisk*>(buf);
        uint32_t remain = limit - b_idx * LINKS_PER_BLOCK;
        uint32_t count  = remain < (uint32_t)LINKS_PER_BLOCK ? remain : (uint32_t)LINKS_PER_BLOCK;

        for (uint32_t s = 0; s < count; ++s) {
            if (arr[s].link_id == LINK_SLOT_FREE) continue;
            if (arr[s].parent_inode == parent_inode && arr[s].child_inode == child_inode) {
                to_free.push_back(b_idx * LINKS_PER_BLOCK + s);
            }
        }
    }

    // 逐个释放（幂等）
    for (uint32_t lid : to_free) {
        uint32_t blk  = sb.link_table_start + lid / LINKS_PER_BLOCK;
        uint32_t slot = lid % LINKS_PER_BLOCK;
        uint8_t buf[BLOCK_SIZE] = {0};
        if (!disk->read_sector(blk, buf)) continue;
        LinkEntryDisk* arr = reinterpret_cast<LinkEntryDisk*>(buf);
        if (arr[slot].link_id != lid) continue;    // 已被并发修改，跳过
        std::memset(&arr[slot], 0, sizeof(LinkEntryDisk));
        arr[slot].link_id = LINK_SLOT_FREE;
        disk->write_sector(blk, buf);
    }
    return true;
}