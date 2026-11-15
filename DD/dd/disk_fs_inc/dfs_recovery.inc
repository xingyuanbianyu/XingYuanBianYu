// ============================================================
// dfs_recovery.inc —— 日志、验证、隐藏三区（per-disk，加固版 v2）
// 语义约定：
//   journal_begin  : 写日志（state=1），拒绝 data > FILE_DATA_CHUNK
//   journal_commit : 真正把日志载荷写入 target_block → 清空日志
//   journal_abort  : 丢弃日志
//   journal_recover: 若存在 state=1，则重放 → 清空日志
//   （commit 写失败时**不清日志**，保持可重试）
// ============================================================

#include <atomic>

// 🚀 txn_id：秒级时间 + 递增计数，避免同秒冲突
static uint32_t next_txn_id() {
    static std::atomic<uint32_t> seq{0};
    uint32_t s = seq.fetch_add(1, std::memory_order_relaxed);
    return ((uint32_t)time(nullptr) << 8) ^ (s & 0xFF);
}

bool DiskFileSystem::journal_begin(BlockDevice* disk, const std::string& path,
                                    const std::string& data, uint32_t target_block) {
    if (!disk) return false;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);

    uint32_t total = total_blocks_of(disk);
    if (total < RESERVED_BLOCKS + 3) return false;
    uint32_t log_start = total - RESERVED_BLOCKS;

    // 🚀 关键修复：拒绝超单块载荷，防止"日志只存一半"造成恢复截断
    if (data.size() > FILE_DATA_CHUNK) {
        std::cerr << "[JOURNAL] 数据 " << data.size()
                  << " 超过单块日志容量 " << FILE_DATA_CHUNK << "，拒绝事务\n";
        return false;
    }
    // 🚀 target 合法范围：数据区
    uint32_t ds = data_start_of(disk);
    if (target_block != 0 && (target_block < ds || target_block >= total - RESERVED_BLOCKS)) {
        std::cerr << "[JOURNAL] target_block 非法: " << target_block << "\n";
        return false;
    }

    JournalHeader jh{};
    jh.magic        = JOURNAL_MAGIC;
    jh.txn_id       = next_txn_id();
    jh.state        = 1;              // Pending
    jh.data_size    = (uint32_t)data.size();
    jh.target_block = target_block;
    strncpy(jh.target_path, path.c_str(), sizeof(jh.target_path) - 1);
    jh.checksum     = simple_checksum(reinterpret_cast<const uint8_t*>(data.data()),
                                      data.size());

    uint8_t buf[BLOCK_SIZE] = {0};
    memcpy(buf, &jh, sizeof(jh));
    if (!data.empty())
        memcpy(buf + sizeof(jh), data.data(), data.size());
    return disk->write_sector(log_start, buf);
}

bool DiskFileSystem::journal_commit(BlockDevice* disk) {
    if (!disk) return false;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);

    uint32_t total = total_blocks_of(disk);
    if (total < RESERVED_BLOCKS + 3) return false;
    uint32_t log_start = total - RESERVED_BLOCKS;

    uint8_t log_buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(log_start, log_buf)) return false;
    JournalHeader* jh = reinterpret_cast<JournalHeader*>(log_buf);
    if (jh->magic != JOURNAL_MAGIC) return true;   // 无待提交
    if (jh->state != 1)            return true;   // 已提交 / 已丢弃

    // 🚀 核心修复：WAL 语义 —— 先把日志载荷写入 target_block
    uint32_t target = jh->target_block;
    uint32_t sz     = jh->data_size;
    uint32_t ds     = data_start_of(disk);
    bool target_valid = (target != 0 && target >= ds &&
                         target < total - RESERVED_BLOCKS &&
                         sz > 0 && sz <= FILE_DATA_CHUNK);

    if (target_valid) {
        // 校验载荷完整性
        uint32_t payload_sum = simple_checksum(
            log_buf + sizeof(JournalHeader), sz);
        if (payload_sum != jh->checksum) {
            std::cerr << "[JOURNAL] 日志载荷 checksum 不匹配，放弃提交\n";
            return false;    // 🚀 不清日志，供上层 abort / 重试
        }

        uint8_t tbuf[BLOCK_SIZE] = {0};
        if (!disk->read_sector(target, tbuf)) {
            // 目标块未初始化 → 允许整块覆盖（后面无旧数据依赖）
            memset(tbuf, 0, BLOCK_SIZE);
        }
        memcpy(tbuf, log_buf + sizeof(JournalHeader), sz);
        if (!disk->write_sector(target, tbuf)) {
            std::cerr << "[JOURNAL] 写 target 失败，保留日志供恢复\n";
            return false;    // 🚀 不清日志
        }
    }
    // target == 0：视为"仅日志不动数据"事务（例如纯元数据操作），也允许
    // target 非 0 但非法：跳过写入，但仍清空日志（视为已消费，防止僵尸 pending）

    // 真正消费掉日志
    uint8_t empty[BLOCK_SIZE] = {0};
    return disk->write_sector(log_start, empty);
}

bool DiskFileSystem::journal_abort(BlockDevice* disk) {
    if (!disk) return false;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);

    uint32_t total = total_blocks_of(disk);
    if (total < RESERVED_BLOCKS + 3) return false;
    uint32_t log_start = total - RESERVED_BLOCKS;

    uint8_t empty[BLOCK_SIZE] = {0};
    return disk->write_sector(log_start, empty);
}

bool DiskFileSystem::journal_has_pending(BlockDevice* disk) {
    if (!disk) return false;
    DiskScope scope(disk);
    std::shared_lock<std::shared_mutex> lock(g_fs_rwlock);

    uint32_t total = total_blocks_of(disk);
    if (total < RESERVED_BLOCKS + 3) return false;
    uint32_t log_start = total - RESERVED_BLOCKS;

    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(log_start, buf)) return false;
    JournalHeader* jh = reinterpret_cast<JournalHeader*>(buf);
    if (jh->magic != JOURNAL_MAGIC) return false;
    return jh->state == 1;
}

std::string DiskFileSystem::journal_recover(BlockDevice* disk) {
    if (!disk) return "";
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);

    uint32_t total = total_blocks_of(disk);
    if (total < RESERVED_BLOCKS + 3) return "";
    uint32_t log_start = total - RESERVED_BLOCKS;

    uint8_t log_buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(log_start, log_buf)) return "";
    JournalHeader* jh = reinterpret_cast<JournalHeader*>(log_buf);

    if (jh->magic != JOURNAL_MAGIC || jh->state != 1) return "";

    std::string path(jh->target_path);
    uint32_t sz     = jh->data_size;
    uint32_t target = jh->target_block;
    uint32_t ds     = data_start_of(disk);

    std::string result = "从日志恢复: " + path + " (" + std::to_string(sz) + " 字节)\n";

    if (sz > FILE_DATA_CHUNK) {
        result += "载荷超过单块容量，日志损坏，放弃恢复。";
        uint8_t empty[BLOCK_SIZE] = {0};
        disk->write_sector(log_start, empty);
        return result;
    }

    // 载荷完整性校验
    uint32_t payload_sum = simple_checksum(log_buf + sizeof(JournalHeader), sz);
    if (payload_sum != jh->checksum) {
        result += "载荷 checksum 损坏，放弃恢复。";
        uint8_t empty[BLOCK_SIZE] = {0};
        disk->write_sector(log_start, empty);
        return result;
    }

    bool valid_target = (target != 0 && target >= ds &&
                         target < total - RESERVED_BLOCKS && sz > 0);

    if (valid_target) {
        uint8_t tbuf[BLOCK_SIZE] = {0};
        if (!disk->read_sector(target, tbuf)) memset(tbuf, 0, BLOCK_SIZE);
        memcpy(tbuf, log_buf + sizeof(JournalHeader), sz);
        if (disk->write_sector(target, tbuf))
            result += "恢复写入成功。";
        else
            result += "恢复写入失败！";
    } else if (target != 0) {
        result += "目标块非法（须位于数据区），放弃恢复。";
    } else {
        result += "该事务未指定目标块，仅消费日志。";
    }

    // 消费日志
    uint8_t empty[BLOCK_SIZE] = {0};
    disk->write_sector(log_start, empty);
    return result;
}

// ============================================================
// 🚀 验证区（verify 区位置：[total-RESERVED+LOG, total-RESERVED+LOG+VERIFY)）
// 修正：
//   - checksum 用 sizeof(Superblock) - sizeof(uint32_t)，与 format 保持一致
//   - 补齐 verify_start 上界检查（< total - HIDDEN_BLOCKS）
// ============================================================
bool DiskFileSystem::verify_write_backup(BlockDevice* disk) {
    if (!disk) return false;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);

    uint32_t total = total_blocks_of(disk);
    if (total < RESERVED_BLOCKS + LOG_BLOCKS + 1) return false;

    uint32_t verify_start = total - RESERVED_BLOCKS + LOG_BLOCKS;
    // 🚀 真正的边界：不能挤到 hidden 区里
    if (verify_start >= total - HIDDEN_BLOCKS) return false;
    if (verify_start + VERIFY_BLOCKS > total - HIDDEN_BLOCKS) return false;

    uint8_t sb_buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(SUPERBLOCK_BLOCK, sb_buf)) return false;

    Superblock sb{};
    memcpy(&sb, sb_buf, sizeof(sb));
    if (sb.magic != SUPER_MAGIC) return false;

    // 🚀 与 format 一致：checksum 字段本身不参与
    uint32_t checksum = simple_checksum(sb_buf, sizeof(Superblock) - sizeof(uint32_t));

    VerifyHeader vh{};
    vh.magic         = VERIFY_MAGIC;
    vh.sb_checksum   = checksum;
    vh.bitmap_blocks = (total / 8) + 1;
    vh.total_files   = 0;
    vh.total_dirs    = 0;

    uint8_t buf[BLOCK_SIZE] = {0};
    memcpy(buf, &vh, sizeof(vh));
    return disk->write_sector(verify_start, buf);
}

bool DiskFileSystem::verify_check(BlockDevice* disk) {
    if (!disk) return false;
    DiskScope scope(disk);
    std::shared_lock<std::shared_mutex> lock(g_fs_rwlock);

    uint32_t total = total_blocks_of(disk);
    if (total < RESERVED_BLOCKS + LOG_BLOCKS + 1) return false;

    uint32_t verify_start = total - RESERVED_BLOCKS + LOG_BLOCKS;
    if (verify_start >= total - HIDDEN_BLOCKS) return false;

    uint8_t sb_buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(SUPERBLOCK_BLOCK, sb_buf)) return false;

    // 🚀 先校验 SB 自身
    Superblock sb{};
    memcpy(&sb, sb_buf, sizeof(sb));
    if (sb.magic != SUPER_MAGIC) return false;
    uint32_t expected_self = simple_checksum(sb_buf, sizeof(Superblock) - sizeof(uint32_t));
    if (sb.checksum != expected_self) return false;

    uint8_t vf_buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(verify_start, vf_buf)) return false;
    VerifyHeader* vh = reinterpret_cast<VerifyHeader*>(vf_buf);
    if (vh->magic != VERIFY_MAGIC) return false;
    return vh->sb_checksum == sb.checksum;   // 🚀 直接比对真实存储值
}

// ============================================================
// 🚀 隐藏区（Hidden 区：[total-HIDDEN, total)）
// 备份 Superblock + 分区表
// ============================================================
bool DiskFileSystem::hidden_write_backup(BlockDevice* disk) {
    if (!disk) return false;
    DiskScope scope(disk);
    std::unique_lock<std::shared_mutex> lock(g_fs_rwlock);

    uint32_t total = total_blocks_of(disk);
    if (total < HIDDEN_BLOCKS) return false;
    uint32_t hidden_start = total - HIDDEN_BLOCKS;

    // 三块内容的静态尺寸校验（编译期定值，但防御性 assert）
    static_assert(sizeof(HiddenHeader) + sizeof(Superblock) + sizeof(PhysicalLogicalMap)
                  <= BLOCK_SIZE, "隐藏区单块备份超出 BLOCK_SIZE");

    uint8_t sb_buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(SUPERBLOCK_BLOCK, sb_buf)) return false;

    uint8_t part_buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(PARTITION_TABLE_BLOCK, part_buf)) {
        memset(part_buf, 0, BLOCK_SIZE);
    }

    uint8_t buf[BLOCK_SIZE] = {0};
    HiddenHeader hh{};
    hh.magic          = HIDDEN_MAGIC;
    hh.sb_backup_size = sizeof(Superblock);
    hh.timestamp      = (uint32_t)time(nullptr);

    memcpy(buf, &hh, sizeof(hh));
    memcpy(buf + sizeof(hh), sb_buf, sizeof(Superblock));
    memcpy(buf + sizeof(hh) + sizeof(Superblock), part_buf, sizeof(PhysicalLogicalMap));

    return disk->write_sector(hidden_start, buf);
}

std::string DiskFileSystem::hidden_read_backup(BlockDevice* disk) {
    if (!disk) return "";
    DiskScope scope(disk);
    std::shared_lock<std::shared_mutex> lock(g_fs_rwlock);

    uint32_t total = total_blocks_of(disk);
    if (total < HIDDEN_BLOCKS) return "";
    uint32_t hidden_start = total - HIDDEN_BLOCKS;

    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(hidden_start, buf)) return "";
    HiddenHeader* hh = reinterpret_cast<HiddenHeader*>(buf);
    if (hh->magic != HIDDEN_MAGIC) return "";

    if (hh->sb_backup_size != sizeof(Superblock)) return "";

    Superblock* sb = reinterpret_cast<Superblock*>(buf + sizeof(HiddenHeader));
    if (sb->magic != SUPER_MAGIC) return "";      // 🚀 SB magic 校验

    uint32_t live_sum = simple_checksum(
        buf + sizeof(HiddenHeader), sizeof(Superblock) - sizeof(uint32_t));
    if (live_sum != sb->checksum) return "[隐藏区] Superblock 备份校验失败";

    PhysicalLogicalMap* pl = reinterpret_cast<PhysicalLogicalMap*>(
        buf + sizeof(HiddenHeader) + sizeof(Superblock));

    std::string result;
    result += "[隐藏区恢复]\n";
    result += "卷标: ";
    result += std::string(sb->volume_label, strnlen(sb->volume_label, 32));
    result += "\n";
    result += "总块数: "   + std::to_string(sb->total_blocks)   + "\n";
    result += "根目录块: " + std::to_string(sb->root_dir_start) + "\n";
    result += "块大小: "   + std::to_string(sb->block_size)     + "\n";

    if (pl->magic == PART_MAGIC && pl->part_count > 0) {
        result += "[分区表备份] 首个分区名: ";
        result += std::string(pl->parts[0].part_name,
                              strnlen(pl->parts[0].part_name, 32));
        result += "\n";
    } else {
        result += "[分区表备份] 无效\n";
    }

    return result;
}