// ==========================================================
// 🚀 查树图：物理图 ↔ 虚拟图 双查
// ==========================================================
static bool read_superblock_raw(BlockDevice* disk, Superblock& sb) {
    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(SUPERBLOCK_BLOCK, buf)) return false;
    std::memcpy(&sb, buf, sizeof(sb));
    return sb.magic == SUPER_MAGIC;
}

static bool read_domain_header_raw(BlockDevice* disk, uint32_t start_block, DomainTableHeader& hdr) {
    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(start_block, buf)) return false;
    std::memcpy(&hdr, buf, sizeof(hdr));
    return hdr.magic == DOMAIN_MAGIC;
}

bool DiskFileSystem::tree_map_check(BlockDevice* disk) {
    if (!disk) return false;
    DiskScope ds(disk);

    // 🚀 注意：这里不要加最外层的 shared_lock！
    // 因为下面的 read_partition_table / domain_read / vbitmap_read 内部都自带锁。
    // 如果这里加了，内部再去获取会导致同一个线程死锁。

    // 1. 物理图（分区表）
    PhysicalLogicalMap pmap{};
    if (!read_partition_table(disk, pmap)) return false;

    // 2. 读超级块和域表头
    Superblock sb{};
    if (!read_superblock_raw(disk, sb)) return false;
    
    DomainTableHeader dth{};
    if (!read_domain_header_raw(disk, sb.domain_table_start, dth)) return false;

    // 3. 虚拟图
    VirtualBitmapHeader vh{};
    std::vector<VirtualBitmapNode> nodes;
    if (!vbitmap_read(disk, vh, nodes)) return false;

    // 4. 数量一致
    if (vh.node_count != dth.domain_count) return false;
    if (vh.node_count != (uint32_t)nodes.size()) return false;

    // 5. 逐项比对
    uint32_t checksum = 0;
    for (uint32_t i = 0; i < nodes.size(); ++i) {
        const auto& n = nodes[i];
        
        // 5.1 位置必须相符
        if (n.node_index != i) return false;

        // 5.2 读取物理域表
        DomainEntry de{};
        if (!domain_read(disk, i, de)) return false;

        // 5.3 状态比对
        bool domain_empty = (de.domain_id == DOMAIN_SLOT_FREE || de.domain_id != i);
        bool vnode_empty  = (n.flags == 0);

        // 如果物理域空了，虚拟节点也必须是空的；反之亦然
        if (domain_empty != vnode_empty) return false;
        
        // 如果非空，inode_start 必须完全一致
        if (!domain_empty && n.inode_start != de.inode_start) return false;

        // 5.4 累加 checksum
        checksum ^= simple_checksum(reinterpret_cast<const uint8_t*>(&n), sizeof(n));
    }
    
    return checksum == vh.checksum;
}

bool DiskFileSystem::tree_map_rebuild_virtual(BlockDevice* disk) {
    return vbitmap_rebuild(disk);
}

bool DiskFileSystem::tree_map_ensure(BlockDevice* disk) {
    if (tree_map_check(disk)) return true;
    return tree_map_rebuild_virtual(disk) && tree_map_check(disk);
}