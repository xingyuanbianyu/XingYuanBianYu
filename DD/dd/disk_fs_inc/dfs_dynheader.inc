// ==========================================================
// 🚀 动态头：块内 [Prefix16B][SectionHdr4B][payload]...
// ==========================================================

// ---------- 内部无锁 helper（供 append 等多步操作保持原子性） ----------

static bool dynamic_header_write_unlocked(BlockDevice* disk, uint32_t block, uint32_t type,
        const std::vector<std::pair<uint16_t, std::string>>& sections) {
    uint8_t buf[BLOCK_SIZE] = {0};
    DynamicHeaderPrefix p{ DYN_HEAD_MAGIC, type, 0, (uint32_t)sections.size() };
    size_t off = sizeof(p);

    for (const auto& s : sections) {
        if (s.second.size() > 0xFFFF) return false; // 单个段负载过长
        size_t need = sizeof(DynamicSectionHeader) + s.second.size();
        if (off + need > BLOCK_SIZE) return false;  // 总长度超块

        DynamicSectionHeader sh{ s.first, (uint16_t)s.second.size() };
        std::memcpy(buf + off, &sh, sizeof(sh)); 
        off += sizeof(sh);
        if (!s.second.empty()) {
            std::memcpy(buf + off, s.second.data(), s.second.size());
            off += s.second.size();
        }
    }
    p.total_size = (uint32_t)off;
    std::memcpy(buf, &p, sizeof(p));
    
    return disk->write_sector(block, buf);
}

static bool dynamic_header_read_unlocked(BlockDevice* disk, uint32_t block, uint32_t& out_type,
        std::vector<std::pair<uint16_t, std::string>>& out_sections) {
    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(block, buf)) return false;

    DynamicHeaderPrefix p{};
    std::memcpy(&p, buf, sizeof(p));
    
    if (p.magic != DYN_HEAD_MAGIC) return false;
    if (p.total_size > BLOCK_SIZE) return false;

    out_type = p.type;
    out_sections.clear();
    size_t off = sizeof(p);
    
    for (uint32_t i = 0; i < p.section_count && off + sizeof(DynamicSectionHeader) <= p.total_size; ++i) {
        DynamicSectionHeader sh{};
        std::memcpy(&sh, buf + off, sizeof(sh)); 
        off += sizeof(sh);
        
        if (off + sh.sec_len > p.total_size) return false;
        
        out_sections.emplace_back(sh.sec_type,
                                  std::string(reinterpret_cast<const char*>(buf + off), sh.sec_len));
        off += sh.sec_len;
    }
    return true;
}

// ==========================================================
// 公开 API
// ==========================================================

bool DiskFileSystem::dynamic_header_write(BlockDevice* disk, uint32_t block, uint32_t type,
        const std::vector<std::pair<uint16_t, std::string>>& sections) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::unique_lock<std::shared_mutex> lk(g_fs_rwlock);

    if (!is_block_valid(disk, block)) return false;
    return dynamic_header_write_unlocked(disk, block, type, sections);
}

bool DiskFileSystem::dynamic_header_read(BlockDevice* disk, uint32_t block, uint32_t& out_type,
        std::vector<std::pair<uint16_t, std::string>>& out_sections) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::shared_lock<std::shared_mutex> lk(g_fs_rwlock);

    if (!is_block_valid(disk, block)) return false;
    return dynamic_header_read_unlocked(disk, block, out_type, out_sections);
}

bool DiskFileSystem::dynamic_header_find(BlockDevice* disk, uint32_t block,
                                         uint16_t sec_type, std::string& out_payload) {
    if (!disk) return false;
    // find 直接调用加过读锁的 read 函数即可，无需再重复加锁
    uint32_t t = 0;
    std::vector<std::pair<uint16_t, std::string>> secs;
    
    if (!dynamic_header_read(disk, block, t, secs)) return false;
    
    for (const auto& s : secs) {
        if (s.first == sec_type) { 
            out_payload = s.second; 
            return true; 
        }
    }
    return false;
}

bool DiskFileSystem::dynamic_header_append(BlockDevice* disk, uint32_t block,
                                           uint16_t sec_type, const std::string& payload) {
    if (!disk) return false;
    DiskScope ds(disk);
    
    // 🚀 核心修复：使用 unique_lock 保证 Read-Modify-Write 的原子性
    // 彻底杜绝两个线程并发 append 导致数据互相覆盖的 TOCTOU 漏洞
    std::unique_lock<std::shared_mutex> lk(g_fs_rwlock);

    if (!is_block_valid(disk, block)) return false;

    uint32_t t = 0;
    std::vector<std::pair<uint16_t, std::string>> secs;
    
    // 使用无锁版读取
    if (!dynamic_header_read_unlocked(disk, block, t, secs)) {
        // 未初始化或读取失败，新建结构
        t = 0; 
        secs.clear();
    }
    
    // 追加新段
    secs.emplace_back(sec_type, payload);
    
    // 使用无锁版写入
    return dynamic_header_write_unlocked(disk, block, t, secs);
}