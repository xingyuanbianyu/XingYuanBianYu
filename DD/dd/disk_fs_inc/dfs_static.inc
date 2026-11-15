// ==========================================================
// 🚀 静态码 (Static Code 196B) —— 读写 + 位图 + 地址
// ==========================================================
bool DiskFileSystem::static_code_read(BlockDevice* disk, uint32_t block, StaticCode& out) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::shared_lock<std::shared_mutex> lk(g_fs_rwlock);

    if (!is_block_valid(disk, block)) return false;

    uint8_t buf[BLOCK_SIZE] = {0}; // 🚀 修复：必须初始化
    if (!disk->read_sector(block, buf)) return false; // 🚀 修复：read_sector
    std::memcpy(&out, buf, sizeof(StaticCode));
    return out.jump.header.magic == STATIC_MAGIC;
}

bool DiskFileSystem::static_code_write(BlockDevice* disk, uint32_t block, const StaticCode& in) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::unique_lock<std::shared_mutex> lk(g_fs_rwlock);

    if (!is_block_valid(disk, block)) return false;

    uint8_t buf[BLOCK_SIZE] = {0};
    std::memcpy(buf, &in, sizeof(StaticCode));   // 196B
    return disk->write_sector(block, buf); // 🚀 修复：write_sector
}

bool DiskFileSystem::static_bit_test(const StaticCode& code, uint32_t bit) {
    if (bit >= STATIC_BITMAP_BITS) return false;
    return (code.bitmap[bit / 8] >> (bit % 8)) & 0x1;
}

void DiskFileSystem::static_bit_set(StaticCode& code, uint32_t bit, bool used) {
    if (bit >= STATIC_BITMAP_BITS) return;
    if (used) code.bitmap[bit / 8] |=  (uint8_t)(1u << (bit % 8));
    else      code.bitmap[bit / 8] &= (uint8_t)~(1u << (bit % 8));
}

bool DiskFileSystem::static_addr_pack(StaticCode& code, uint32_t part_block,
                                      uint32_t domain_block, uint32_t link_block) {
    auto pack = [](uint8_t* dst, uint32_t v) {
        std::memcpy(dst, &v, 4);
        dst[4] = 0; dst[5] = 0;
    };
    pack(code.addr.part_addr,   part_block);
    pack(code.addr.domain_addr, domain_block);
    pack(code.addr.link_addr,   link_block);
    return true;
}