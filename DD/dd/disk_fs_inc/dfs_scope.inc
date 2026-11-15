// ==========================================================
// 🚀 作用块 (Scope Block) 实现 v3
// 布局（与 disk_fs.h 严格对齐）：
//   block 2                  : ScopeTableHeader (16B)
//   block 3 .. 5             : ScopeEntry 数组，每块 SCOPE_PER_BLOCK 个
// 容量上限：SCOPE_MAX_ENTRIES = (SCOPE_TABLE_BLOCKS_DEF - 1) * SCOPE_PER_BLOCK = 180
// ==========================================================

static constexpr uint32_t SCOPE_MAX_ENTRIES =
    (SCOPE_TABLE_BLOCKS_DEF - 1) * SCOPE_PER_BLOCK;

// ---------- 内部无锁 helper ----------

static bool scope_hdr_read_unlocked(BlockDevice* disk, ScopeTableHeader& hdr) {
    if (!disk) return false;
    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(SCOPE_TABLE_BLOCK, buf)) return false;
    std::memcpy(&hdr, buf, sizeof(hdr));
    return hdr.magic == SCOPE_MAGIC && hdr.scope_count <= SCOPE_MAX_ENTRIES;
}

static bool scope_hdr_write_unlocked(BlockDevice* disk, const ScopeTableHeader& hdr) {
    if (!disk) return false;
    uint8_t buf[BLOCK_SIZE] = {0};
    std::memcpy(buf, &hdr, sizeof(hdr));
    return disk->write_sector(SCOPE_TABLE_BLOCK, buf);
}

static bool scope_read_unlocked(BlockDevice* disk, uint32_t id, ScopeEntry& out) {
    if (!disk || id >= SCOPE_MAX_ENTRIES) return false;
    uint32_t blk = SCOPE_TABLE_BLOCK + 1 + id / SCOPE_PER_BLOCK;
    uint32_t off = (id % SCOPE_PER_BLOCK) * sizeof(ScopeEntry);
    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(blk, buf)) return false;
    std::memcpy(&out, buf + off, sizeof(ScopeEntry));
    return out.scope_id == id;
}

static bool scope_write_unlocked(BlockDevice* disk, uint32_t id, const ScopeEntry& in) {
    if (!disk || id >= SCOPE_MAX_ENTRIES) return false;
    uint32_t blk = SCOPE_TABLE_BLOCK + 1 + id / SCOPE_PER_BLOCK;
    uint32_t off = (id % SCOPE_PER_BLOCK) * sizeof(ScopeEntry);
    uint8_t buf[BLOCK_SIZE] = {0};
    if (!disk->read_sector(blk, buf)) std::memset(buf, 0, BLOCK_SIZE);
    std::memcpy(buf + off, &in, sizeof(ScopeEntry));
    return disk->write_sector(blk, buf);
}

// 首次初始化：清空 3..5，写 header + root scope
static bool scope_ensure_table_unlocked(BlockDevice* disk) {
    ScopeTableHeader hdr{};
    if (scope_hdr_read_unlocked(disk, hdr)) return true;

    // 🚀 清空所有 entries 块，避免旧数据残留被 scope_read 误读
    uint8_t zero[BLOCK_SIZE] = {0};
    for (uint32_t b = 1; b < SCOPE_TABLE_BLOCKS_DEF; ++b)
        disk->write_sector(SCOPE_TABLE_BLOCK + b, zero);

    hdr.magic            = SCOPE_MAGIC;
    hdr.scope_count      = 1;
    hdr.next_table_block = 0;
    hdr.root_scope_id    = SCOPE_ROOT_ID;

    ScopeEntry root{};
    root.scope_id    = SCOPE_ROOT_ID;
    root.parent_id   = SCOPE_ROOT_ID;
    root.level       = 0;
    root.child_count = 0;
    std::strncpy(root.name, "0", sizeof(root.name) - 1);

    if (!scope_write_unlocked(disk, SCOPE_ROOT_ID, root)) return false;
    return scope_hdr_write_unlocked(disk, hdr);
}

// 内部通用 alloc（调用者必须已持 unique_lock）
static uint32_t scope_alloc_unlocked(BlockDevice* disk, uint32_t parent_id, uint32_t level) {
    if (!scope_ensure_table_unlocked(disk)) return 0;

    ScopeTableHeader hdr{};
    if (!scope_hdr_read_unlocked(disk, hdr)) return 0;

    ScopeEntry parent{};
    if (!scope_read_unlocked(disk, parent_id, parent)) return 0;

    if (hdr.scope_count >= SCOPE_MAX_ENTRIES) {
        std::cerr << "[SCOPE] 条目已满 (" << SCOPE_MAX_ENTRIES << ")\n";
        return 0;
    }

    uint32_t child_idx = parent.child_count;
    uint32_t new_id    = hdr.scope_count;
    if (new_id == parent_id) return 0;   // 防自指

    std::string nm = DiskFileSystem::scope_make_name(level, child_idx);
    if (nm.empty()) return 0;

    ScopeEntry e{};
    e.scope_id    = new_id;
    e.parent_id   = parent_id;
    e.level       = level;
    e.inode_id    = 0;
    e.child_count = 0;
    std::strncpy(e.name, nm.c_str(), sizeof(e.name) - 1);

    if (!scope_write_unlocked(disk, new_id, e)) return 0;

    parent.child_count = child_idx + 1;
    if (!scope_write_unlocked(disk, parent_id, parent)) return 0;

    hdr.scope_count = new_id + 1;
    if (!scope_hdr_write_unlocked(disk, hdr)) return 0;

    return new_id;
}

// ---------- 公开 API ----------

std::string DiskFileSystem::scope_make_name(uint32_t level, uint32_t child_index) {
    if (level == 0) return "0";

    // 🚀 深度保护：level 个字母 + 1 个数字，需严格小于 SCOPE_NAME_MAX
    if (level + 1 > SCOPE_NAME_MAX - 1) return "";

    uint32_t digit      = child_index % SCOPE_DIGIT_COUNT;
    uint32_t letter_idx = child_index / SCOPE_DIGIT_COUNT;

    // 🚀 字母组合上限：26^level
    uint64_t max_letters = 1;
    for (uint32_t i = 0; i < level; ++i) max_letters *= SCOPE_LETTER_COUNT;
    if ((uint64_t)letter_idx >= max_letters) return "";

    std::string letters(level, 'A');
    for (int i = (int)level - 1; i >= 0; --i) {
        letters[i]  = (char)('A' + (letter_idx % SCOPE_LETTER_COUNT));
        letter_idx /= SCOPE_LETTER_COUNT;
    }
    std::string out = letters;
    out += (char)('0' + digit);
    return out;
}

bool DiskFileSystem::scope_read(BlockDevice* disk, uint32_t scope_id, ScopeEntry& out) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::shared_lock<std::shared_mutex> lk(g_fs_rwlock);
    return scope_read_unlocked(disk, scope_id, out);
}

bool DiskFileSystem::scope_write(BlockDevice* disk, uint32_t scope_id, const ScopeEntry& in) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::unique_lock<std::shared_mutex> lk(g_fs_rwlock);
    return scope_write_unlocked(disk, scope_id, in);
}

uint32_t DiskFileSystem::scope_alloc(BlockDevice* disk, uint32_t parent_id, uint32_t level) {
    if (!disk) return 0;
    DiskScope ds(disk);
    std::unique_lock<std::shared_mutex> lk(g_fs_rwlock);
    return scope_alloc_unlocked(disk, parent_id, level);
}

bool DiskFileSystem::scope_free(BlockDevice* disk, uint32_t scope_id) {
    if (!disk || scope_id == SCOPE_ROOT_ID) return false;
    DiskScope ds(disk);
    std::unique_lock<std::shared_mutex> lk(g_fs_rwlock);

    ScopeEntry e{};
    if (!scope_read_unlocked(disk, scope_id, e)) return false;

    // 🚀 核心修复：**不动 parent.child_count**
    //    原因：child_count 是"已分配过的名字编号总和"，若回退会与已存在的 A2/A3 名字冲突。
    //    正确做法是：free 后名字空洞永久保留，新 alloc 从末尾继续，同名永不复用。
    ScopeEntry empty{};
    return scope_write_unlocked(disk, scope_id, empty);
}

bool DiskFileSystem::scope_enter(BlockDevice* disk, uint32_t parent_scope_id, uint32_t& out_child_id) {
    if (!disk) return false;
    DiskScope ds(disk);
    std::unique_lock<std::shared_mutex> lk(g_fs_rwlock);

    // 🚀 单锁事务：读取 parent 与生成 child 在同一次 unique_lock 内完成，
    //    避免 parent 在 read 和 alloc 之间被其他线程删除
    ScopeEntry parent{};
    if (!scope_read_unlocked(disk, parent_scope_id, parent)) return false;

    uint32_t id = scope_alloc_unlocked(disk, parent_scope_id, parent.level + 1);
    if (id == 0) return false;
    out_child_id = id;
    return true;
}

bool DiskFileSystem::scope_back(BlockDevice* disk, uint32_t scope_id, uint32_t& out_parent_id) {
    ScopeEntry e{};
    if (!scope_read(disk, scope_id, e)) return false;
    out_parent_id = e.parent_id;
    return true;
}

std::string DiskFileSystem::scope_path_string(BlockDevice* disk, uint32_t scope_id) {
    std::vector<std::string> parts;
    uint32_t cur = scope_id;
    for (int guard = 0; guard < 256; ++guard) {
        ScopeEntry e{};
        if (!scope_read(disk, cur, e)) break;
        parts.push_back(e.name);
        if (e.parent_id == cur) break;   // root 自指
        cur = e.parent_id;
    }
    if (parts.empty()) return "";        // 🚀 scope_id 不存在
    std::string out;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        out += *it;
        if (it + 1 != parts.rend()) out += "/";
    }
    return out;
}