// ============================================================
// dfs_super.inc —— 超级块读写、格式化、位图重建（适配名字分离架构）
// ============================================================

// 🚀 前向声明，让 format 能用到名字块写入
static uint32_t write_name_to_blocks(BlockDevice* disk, const std::string& name);

// ---------- 超级块读写 ----------
static bool read_super(BlockDevice* disk, Superblock& sb) {
    if (!disk) return false;
    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(SUPERBLOCK_BLOCK, buf)) return false;
    memcpy(&sb, buf, sizeof(Superblock));
    return sb.magic == SUPER_MAGIC;
}

static bool write_super(BlockDevice* disk, const Superblock& sb_in) {
    if (!disk) return false;
    Superblock sb = sb_in;
    
    sb.checksum = 0;
    sb.checksum = DiskFileSystem::simple_checksum(
        reinterpret_cast<const uint8_t*>(&sb), 
        sizeof(Superblock) - sizeof(uint32_t));

    uint8_t buf[BLOCK_SIZE] = {0};
    memcpy(buf, &sb, sizeof(Superblock));
    return disk->write_sector(SUPERBLOCK_BLOCK, buf);
}

bool DiskFileSystem::get_mounted_flag(BlockDevice* disk) {
    if (!disk) return false;
    std::shared_lock<std::shared_mutex> lock(g_fs_rwlock);
    Superblock sb;
    if (!read_super(disk, sb)) return false;
    return sb.mounted == 1;
}

void DiskFileSystem::set_mounted_flag(BlockDevice* disk, bool mounted) {
    if (!disk) return;
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);
    Superblock sb;
    if (!read_super(disk, sb)) return;
    sb.mounted = mounted ? 1 : 0;
    write_super(disk, sb);
}

// ============================================================
// 🚀 格式化
// ============================================================
void DiskFileSystem::format(BlockDevice* disk, const std::string& label, bool create_defaults) {
    if (!disk) return;
    DiskScope scope(disk);

    uint32_t& g_total = total_blocks_of(disk);
    g_total = disk->get_total_sectors();

    const uint32_t inode_table_start  = INODE_TABLE_START_BLOCK_DEF;
    const uint32_t inode_table_blocks = INODE_TABLE_BLOCKS_DEF;
    const uint32_t inode_count        = inode_table_blocks * INODE_PER_BLOCK;

    const uint32_t domain_table_start  = DOMAIN_TABLE_START_BLOCK_DEF;
    const uint32_t domain_table_blocks = DOMAIN_TABLE_BLOCKS_DEF;

    const uint32_t link_table_start  = LINK_TABLE_START_BLOCK_DEF;
    const uint32_t link_table_blocks = LINK_TABLE_BLOCKS_DEF;

    const uint32_t data_start      = DATA_START_BLOCK_DEF;   
    const uint32_t root_dir_start  = data_start;             

    if (g_total < data_start + RESERVED_BLOCKS + 16) {
        std::cerr << "[FS] 磁盘容量过小，拒绝格式化。" << std::endl;
        return;
    }

    block_bitmap_of(disk).assign((g_total / 8) + 1, 0);

    Superblock sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic               = SUPER_MAGIC;
    sb.total_blocks        = g_total;
    sb.root_dir_start      = root_dir_start;
    sb.data_start          = data_start;
    sb.free_blocks         = g_total - data_start - RESERVED_BLOCKS;
    sb.mounted             = 0;
    sb.inode_table_start   = inode_table_start;
    sb.inode_table_blocks  = inode_table_blocks;
    sb.inode_count         = inode_count;
    sb.domain_table_start  = domain_table_start;
    sb.domain_table_blocks = domain_table_blocks;
    sb.domain_count        = 1;
    sb.link_table_start    = link_table_start;
    sb.link_table_blocks   = link_table_blocks;
    sb.link_count          = 0;
    sb.block_size          = BLOCK_SIZE;
    sb.reserved_blocks     = RESERVED_BLOCKS;
    strncpy(sb.volume_label, label.c_str(), 31);

    for (uint32_t i = 0; i < data_start; ++i) block_bitmap_set(i, true);
    block_bitmap_set(root_dir_start, true);

    write_super(disk, sb);

    // ============================================================
    // 🚀 分区表（第 1 块）
    // ============================================================
    PhysicalLogicalMap pl_map;
    memset(&pl_map, 0, sizeof(pl_map));
    pl_map.magic = PART_MAGIC;
    pl_map.part_count = 1;
    std::string part_name = label.empty() ? "A" : label;
    strncpy(pl_map.parts[0].part_name, part_name.c_str(), 31);
    pl_map.parts[0].start_block = data_start;
    pl_map.parts[0].end_block   = g_total - RESERVED_BLOCKS - 1;
    pl_map.parts[0].root_inode  = 2;
    pl_map.parts[0].type        = 1;

    uint8_t part_buffer[BLOCK_SIZE] = {0};
    memcpy(part_buffer, &pl_map, sizeof(pl_map));
    disk->write_sector(PARTITION_TABLE_BLOCK, part_buffer);

    // ============================================================
    // 🚀 作用块表：header @2，entries @3..6
    // ============================================================
    {
        ScopeTableHeader sth{};
        sth.magic            = SCOPE_MAGIC;
        sth.scope_count      = 1;
        sth.next_table_block = 0;
        sth.root_scope_id    = SCOPE_ROOT_ID;
        uint8_t hbuf[BLOCK_SIZE] = {0};
        memcpy(hbuf, &sth, sizeof(sth));
        disk->write_sector(SCOPE_TABLE_BLOCK, hbuf);

        ScopeEntry root_scope{};
        root_scope.scope_id    = SCOPE_ROOT_ID;
        root_scope.parent_id   = SCOPE_ROOT_ID;
        root_scope.level       = 0;
        root_scope.child_count = 0;
        strncpy(root_scope.name, "0", sizeof(root_scope.name) - 1);
        uint8_t ebuf[BLOCK_SIZE] = {0};
        memcpy(ebuf, &root_scope, sizeof(root_scope));
        disk->write_sector(SCOPE_TABLE_BLOCK + 1, ebuf);

        uint8_t zero[BLOCK_SIZE] = {0};
        for (uint32_t b = 2; b < SCOPE_TABLE_BLOCKS_DEF; ++b)
            disk->write_sector(SCOPE_TABLE_BLOCK + b, zero);
    }

    // ============================================================
    // 🚀 虚拟位图：header @7，nodes @8..11
    // ============================================================
    {
        VirtualBitmapHeader vh{};
        vh.magic      = VBITMAP_MAGIC;
        vh.node_count = 1;
        vh.next_block = 0;

        VirtualBitmapNode n{};
        n.node_index  = 0;
        n.domain_id   = 0;
        n.inode_start = inode_table_start;
        n.flags       = 1;
        vh.checksum = simple_checksum(reinterpret_cast<const uint8_t*>(&n), sizeof(n));

        uint8_t hbuf[BLOCK_SIZE] = {0};
        memcpy(hbuf, &vh, sizeof(vh));
        disk->write_sector(VBITMAP_TABLE_BLOCK, hbuf);

        uint8_t nbuf[BLOCK_SIZE] = {0};
        memcpy(nbuf, &n, sizeof(n));
        disk->write_sector(VBITMAP_TABLE_BLOCK + 1, nbuf);

        uint8_t zero[BLOCK_SIZE] = {0};
        for (uint32_t b = 2; b < VBITMAP_TABLE_BLOCKS_DEF; ++b)
            disk->write_sector(VBITMAP_TABLE_BLOCK + b, zero);
    }

    // ============================================================
    // 🚀 物理逻辑头
    // ============================================================
    {
        uint8_t zero[BLOCK_SIZE] = {0};
        for (uint32_t i = 0; i < MAX_PARTITIONS; ++i)
            disk->write_sector(PLH_TABLE_BLOCK + i, zero);
    }

    // ============================================================
    // 🚀 Inode 表清零 + 根 inode (2)
    // ============================================================
    for (uint32_t i = 0; i < inode_table_blocks; ++i) {
        uint8_t empty[BLOCK_SIZE] = {0};
        disk->write_sector(inode_table_start + i, empty);
    }
    inode_bitmap_of(disk).assign((inode_count / 8) + 1, 0);

    InodeEntry root_ie;
    memset(&root_ie, 0, sizeof(root_ie));
    root_ie.inode_id     = 2;
    root_ie.type         = 1;
    root_ie.start_block  = root_dir_start;
    root_ie.parent_inode = 0;
    root_ie.link_count   = 1;
    strncpy(root_ie.resource_tag, "R0", 7);

    uint8_t inode_buf[BLOCK_SIZE] = {0};
    uint32_t root_blk  = inode_table_start + 2 / INODE_PER_BLOCK;
    uint32_t root_slot = 2 % INODE_PER_BLOCK;
    disk->read_sector(root_blk, inode_buf);
    reinterpret_cast<InodeEntry*>(inode_buf)[root_slot] = root_ie;
    disk->write_sector(root_blk, inode_buf);
    inode_bitmap_set(disk, 2, true);

    // ============================================================
    // 🚀 根目录内容（适配名字分离）
    // ============================================================
    uint8_t root_buf[BLOCK_SIZE] = {0};
    size_t  dir_offset = 0;

    if (create_defaults) {
        struct InitDir { const char* name; const char* tag; };
        const InitDir init_dirs[] = {
            {"home", "HOME"}, {"usr", "R0"}, {"xyby", "R0"},
            {"bin", "R0"}, {"lib", "R0"}, {"dev", "R0"}
        };
        const int N = (int)(sizeof(init_dirs) / sizeof(init_dirs[0]));

        for (int i = 0; i < N; ++i) {
            uint32_t ci = 3 + i;
            inode_bitmap_set(disk, ci, true);

            uint32_t db = data_start + 1 + i;    
            block_bitmap_set(db, true);

            InodeEntry cie;
            memset(&cie, 0, sizeof(cie));
            cie.inode_id     = ci;
            cie.type         = 1;
            cie.start_block  = db;
            cie.parent_inode = 2;
            cie.link_count   = 1;
            strncpy(cie.resource_tag, init_dirs[i].tag, 7);

            uint32_t cblk  = inode_table_start + ci / INODE_PER_BLOCK;
            uint32_t cslot = ci % INODE_PER_BLOCK;
            uint8_t cbuf[BLOCK_SIZE] = {0};
            disk->read_sector(cblk, cbuf);
            reinterpret_cast<InodeEntry*>(cbuf)[cslot] = cie;
            disk->write_sector(cblk, cbuf);

            uint8_t empty_dir[BLOCK_SIZE] = {0};
            disk->write_sector(db, empty_dir);

            // 🚀 适配定长 40 字节索引
            std::string dir_name = init_dirs[i].name;
            size_t name_len = dir_name.size();
            
            if (dir_offset + sizeof(DirEntryHeader) > BLOCK_SIZE) break;

            // 1. 写入名字到独立的名字块
            uint32_t name_blk = write_name_to_blocks(disk, dir_name);

            // 2. 写入定长索引到根目录块
            DirEntryHeader* hdr = reinterpret_cast<DirEntryHeader*>(root_buf + dir_offset);
            memset(hdr, 0, sizeof(*hdr));
            hdr->inode_id      = ci;
            hdr->type          = 1;
            hdr->start_sector  = db;
            hdr->parent_sector = root_dir_start;
            hdr->size          = 0;
            hdr->name_len      = (uint16_t)name_len;
            hdr->name_block    = name_blk; // 记录名字块
            strncpy(hdr->resource_tag, init_dirs[i].tag, 7);
            
            dir_offset += sizeof(DirEntryHeader);
        }
    }

    disk->write_sector(root_dir_start, root_buf);   

    // ============================================================
    // 🚀 域表：header + entry[0]
    // ============================================================
    {
        uint8_t dh[BLOCK_SIZE] = {0};
        DomainTableHeader dth;
        memset(&dth, 0, sizeof(dth));
        dth.magic = DOMAIN_MAGIC;
        dth.domain_count = 1;
        memcpy(dh, &dth, sizeof(dth));
        disk->write_sector(domain_table_start, dh);

        uint8_t dbuf[BLOCK_SIZE] = {0};
        DomainEntry* darr = reinterpret_cast<DomainEntry*>(dbuf);
        memset(&darr[0], 0, sizeof(DomainEntry));
        darr[0].domain_id   = 0;
        darr[0].parent_id   = 0xFFFFFFFF;
        darr[0].inode_start = inode_table_start;
        darr[0].inode_count = inode_count;
        darr[0].data_start  = data_start;
        darr[0].data_end    = g_total - RESERVED_BLOCKS;
        strncpy(darr[0].name, "main", 31);
        disk->write_sector(domain_table_start + 1, dbuf);
    }

    // ============================================================
    // 🚀 链接表清零
    // ============================================================
    for (uint32_t i = 0; i < link_table_blocks; ++i) {
        uint8_t empty[BLOCK_SIZE] = {0};
        disk->write_sector(link_table_start + i, empty);
    }

    // ============================================================
    // 🚀 末尾保留区
    // ============================================================
    const uint32_t log_start    = g_total - RESERVED_BLOCKS;
    const uint32_t verify_start = log_start + LOG_BLOCKS;
    const uint32_t hidden_start = g_total - HIDDEN_BLOCKS;

    for (uint32_t i = 0; i < LOG_BLOCKS; ++i) {
        uint8_t empty[BLOCK_SIZE] = {0};
        disk->write_sector(log_start + i, empty);
    }

    uint8_t verify_buf[BLOCK_SIZE] = {0};
    memcpy(verify_buf, &sb, sizeof(Superblock));
    disk->write_sector(verify_start, verify_buf);

    uint8_t hidden_buf[BLOCK_SIZE] = {0};
    HiddenHeader hh;
    memset(&hh, 0, sizeof(hh));
    hh.magic            = HIDDEN_MAGIC;
    hh.sb_backup_size   = sizeof(Superblock);
    hh.timestamp        = (uint32_t)time(nullptr);
    memcpy(hidden_buf, &hh, sizeof(hh));
    memcpy(hidden_buf + sizeof(hh), &sb, sizeof(Superblock));
    disk->write_sector(hidden_start, hidden_buf);

    for (uint32_t i = g_total - RESERVED_BLOCKS; i < g_total; ++i)
        block_bitmap_set(i, true);

    alloc_state_invalidate(disk);     

    std::cerr << "[FS] format 完成: total=" << g_total
              << " data_start=" << data_start
              << " root=" << root_dir_start
              << " inode_table=" << inode_table_start
              << " domain_table=" << domain_table_start
              << " link_table=" << link_table_start
              << std::endl;
}

// ============================================================
// 🚀 位图重建（适配名字分离）
// ============================================================
void DiskFileSystem::rebuild_bitmap(BlockDevice* disk) {
    if (!disk) return;
    DiskScope scope(disk);

    Superblock sb;
    if (!read_super(disk, sb)) {
        std::cerr << "[FS] 超级块无效，拒绝挂载。\n";
        total_blocks_of(disk) = 0;
        return;
    }
    if (sb.block_size != BLOCK_SIZE) {
        std::cerr << "[FS] 块大小不匹配，拒绝挂载。\n";
        total_blocks_of(disk) = 0;
        return;
    }

    uint32_t& g_total = total_blocks_of(disk);
    g_total = disk->get_total_sectors();

    if (g_total < sb.data_start + RESERVED_BLOCKS + 1) {
        std::cerr << "[FS] 磁盘容量与超级块不匹配，拒绝挂载。\n";
        total_blocks_of(disk) = 0;
        return;
    }

    data_start_invalidate(disk);

    block_bitmap_of(disk).assign((g_total / 8) + 1, 0);
    inode_bitmap_of(disk).assign((sb.inode_count / 8) + 1, 0);

    for (uint32_t i = 0; i < sb.data_start; ++i) block_bitmap_set(i, true);
    if (sb.root_dir_start < g_total) block_bitmap_set(sb.root_dir_start, true);

    for (uint32_t i = 0; i < sb.inode_table_blocks; ++i)
        block_bitmap_set(sb.inode_table_start + i, true);
    for (uint32_t i = 0; i < sb.domain_table_blocks; ++i)
        block_bitmap_set(sb.domain_table_start + i, true);
    for (uint32_t i = 0; i < sb.link_table_blocks; ++i)
        block_bitmap_set(sb.link_table_start + i, true);

    for (uint32_t i = 0; i < SCOPE_TABLE_BLOCKS_DEF; ++i)
        block_bitmap_set(SCOPE_TABLE_BLOCK + i, true);
    for (uint32_t i = 0; i < VBITMAP_TABLE_BLOCKS_DEF; ++i)
        block_bitmap_set(VBITMAP_TABLE_BLOCK + i, true);
    for (uint32_t i = 0; i < MAX_PARTITIONS; ++i)
        block_bitmap_set(PLH_TABLE_BLOCK + i, true);

    for (uint32_t i = 2; i < sb.inode_count; ++i) {
        uint32_t blk  = sb.inode_table_start + i / INODE_PER_BLOCK;
        uint32_t slot = i % INODE_PER_BLOCK;
        uint8_t buf[BLOCK_SIZE] = {0};
        if (!disk->read_sector(blk, buf)) continue;
        InodeEntry ie = reinterpret_cast<InodeEntry*>(buf)[slot];
        
        if (ie.inode_id == 0 || ie.inode_id != i) continue;
        
        inode_bitmap_set(disk, i, true);

        std::set<uint32_t> visited;
        uint32_t b = ie.start_block;
        while (b != 0 && b < g_total && !visited.count(b)) {
            visited.insert(b);
            block_bitmap_set(b, true);
            uint8_t data_buf[BLOCK_SIZE] = {0};
            if (!disk->read_sector(b, data_buf)) break;

            if (ie.type == 0) {
                uint32_t next = 0;
                memcpy(&next, data_buf + FILE_DATA_CHUNK, 4);
                b = next;
            } else {
                // 🚀 适配定长目录遍历 + 回收名字块
                uint32_t next = 0;
                for (size_t off = 0; off + sizeof(DirEntryHeader) <= BLOCK_SIZE; off += sizeof(DirEntryHeader)) {
                    DirEntryHeader* hdr = reinterpret_cast<DirEntryHeader*>(data_buf + off);
                    
                    if (hdr->inode_id == 0xFFFFFFFEu) {
                        next = hdr->start_sector;
                        break;
                    }
                    if (hdr->inode_id == 0) continue;

                    // 标记名字块占用
                    if (hdr->name_block != 0) {
                        uint32_t nb = hdr->name_block;
                        std::set<uint32_t> visited_name;
                        while (nb != 0 && nb < g_total && !visited_name.count(nb)) {
                            visited_name.insert(nb);
                            block_bitmap_set(nb, true);
                            uint8_t nbuf[BLOCK_SIZE] = {0};
                            if (disk->read_sector(nb, nbuf)) {
                                NameBlockHeader* nbh = reinterpret_cast<NameBlockHeader*>(nbuf);
                                if (nbh->magic == NAME_BLOCK_MAGIC) {
                                    nb = nbh->next_block;
                                } else {
                                    break;
                                }
                            } else {
                                break;
                            }
                        }
                    }
                }
                b = next;
            }
        }

        if (ie.type == 0 && ie.indirect_block != 0 && ie.indirect_block < g_total) {
            block_bitmap_set(ie.indirect_block, true);
            uint8_t ibuf[BLOCK_SIZE] = {0};
            if (disk->read_sector(ie.indirect_block, ibuf)) {
                const IndexBlock* ib = reinterpret_cast<const IndexBlock*>(ibuf);
                if (ib->magic == INDEX_MAGIC && ib->count <= INDEX_PTR_MAX) {
                    for (uint32_t k = 0; k < ib->count; ++k) {
                        uint32_t p = ib->pointers[k];
                        if (p != 0 && p < g_total) {
                            block_bitmap_set(p, true);
                        }
                    }
                }
            }
        }
    }

    for (uint32_t i = g_total - RESERVED_BLOCKS; i < g_total; ++i)
        block_bitmap_set(i, true);

    alloc_state_invalidate(disk);
}

// ============================================================
// 🚀 物理逻辑图（分区表）读写
// ============================================================
bool DiskFileSystem::read_partition_table(BlockDevice* disk, PhysicalLogicalMap& out_map) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::shared_lock<std::shared_mutex> lock(g_fs_rwlock);

    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(PARTITION_TABLE_BLOCK, buf)) return false;

    std::memcpy(&out_map, buf, sizeof(PhysicalLogicalMap));
    if (out_map.magic != PART_MAGIC) return false;
    if (out_map.part_count > MAX_PARTITIONS) return false;

    return true;
}

bool DiskFileSystem::write_partition_table(BlockDevice* disk, const PhysicalLogicalMap& in_map) {
    if (!disk) return false;
    if (in_map.magic != PART_MAGIC) return false;
    if (in_map.part_count > MAX_PARTITIONS) return false;

    DiskScope ds(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);

    uint8_t buf[BLOCK_SIZE] = {0};
    std::memcpy(buf, &in_map, sizeof(PhysicalLogicalMap));
    
    if (!disk->write_sector(PARTITION_TABLE_BLOCK, buf)) return false;

    plh_sync_from_partition(disk);
    return true;
}