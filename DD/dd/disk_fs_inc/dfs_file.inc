// ============================================================
// dfs_file.inc —— 文件创建、写入、读取（名字分离架构适配版）
// 
// 核心原则：
//   1. write_file 采用 **写时复制 (COW)**：先分配新块 → 写数据 → 更新 inode → 更新目录项 → 释放旧块。
//   2. 目录项更新已适配定长索引 (DirEntryHeader) + 名字块 (NameBlock) 架构。
// ============================================================

// 内部的"释放 inode 数据链"（复用 dfs_inode.inc 思路，避免依赖）
static void file_free_old_data_chain(BlockDevice* disk, const InodeEntry& ie) {
    if (!disk) return;
    uint32_t total = total_blocks_of(disk);

    // ---- 1. 释放 start_block 链 ----
    uint32_t b = ie.start_block;
    std::set<uint32_t> visited;
    while (b != 0 && b < total && is_block_valid(disk, b) && !visited.count(b)) {
        visited.insert(b);
        uint8_t buf[BLOCK_SIZE] = {0};
        uint32_t next = 0;
        if (disk->read_sector(b, buf))
            std::memcpy(&next, buf + FILE_DATA_CHUNK, 4);
        DiskFileSystem::free_sector(disk, b);
        b = next;
    }

    // ---- 2. 释放 indirect_block 的 pointers + 索引块本身 ----
    if (ie.indirect_block != 0 && is_block_valid(disk, ie.indirect_block)) {
        uint8_t ibuf[BLOCK_SIZE] = {0};
        if (disk->read_sector(ie.indirect_block, ibuf)) {
            const IndexBlock* ib = reinterpret_cast<const IndexBlock*>(ibuf);
            if (ib->magic == INDEX_MAGIC && ib->count <= INDEX_PTR_MAX) {
                std::set<uint32_t> freed;
                for (uint32_t k = 0; k < ib->count; ++k) {
                    uint32_t p = ib->pointers[k];
                    if (p == 0 || p == ie.indirect_block || p >= total) continue;
                    if (freed.count(p)) continue;
                    freed.insert(p);
                    DiskFileSystem::free_sector(disk, p);
                }
            }
        }
        DiskFileSystem::free_sector(disk, ie.indirect_block);
    }
}

// ============================================================
// create_file
// ============================================================
bool DiskFileSystem::create_file(BlockDevice* disk, const std::string& vfs_path) {
    if (!disk) return false;
    DiskScope scope(disk);

    std::string name = get_filename(vfs_path);
    if (name.empty() || name.size() > LINK_NAME_CAP || name == "/") {
        std::cerr << "[CREATE_FILE] 名字为空或超长\n";
        return false;
    }

    std::string parent_path = get_parent_path(vfs_path);
    uint32_t parent_inode = path_to_inode(disk, parent_path);
    
    // 🚀 核心防御：父目录不存在，直接拒绝，绝不放回干目录
    if (parent_inode == 0) {
        std::cerr << "[DFS ERROR] 父目录不存在，拒绝创建: " << vfs_path << "\n";
        return false;
    }

    if (find_inode_in_dir(disk, parent_inode, name) != 0) return false;

    InodeEntry pie;
    if (!inode_read(disk, parent_inode, pie)) return false;

    uint32_t    parent_domain = pie.domain_id;
    std::string new_tag       = "CMD";
    uint32_t    new_domain_id = DOMAIN_SLOT_FREE; 

    if (pie.resource_tag[0] != '\0') {
        new_tag = std::string(pie.resource_tag, strnlen(pie.resource_tag, 8));
    } else {
        new_domain_id = domain_alloc(disk, parent_domain, name);
        if (new_domain_id != DOMAIN_SLOT_FREE && new_domain_id != 0) {
            parent_domain = new_domain_id;
            new_tag = "D" + std::to_string(new_domain_id);   // 控制长度
        }
    }

    uint32_t ci = inode_alloc(disk, 0, parent_inode, parent_domain);
    if (ci == 0) {
        if (new_domain_id != DOMAIN_SLOT_FREE && new_domain_id != 0)
            domain_free(disk, new_domain_id);
        return false;
    }

    uint32_t db = allocate_sector(disk);
    if (db == 0) {
        inode_free(disk, ci);
        if (new_domain_id != DOMAIN_SLOT_FREE && new_domain_id != 0)
            domain_free(disk, new_domain_id);
        return false;
    }

    InodeEntry cie;
    if (!inode_read(disk, ci, cie)) {
        free_sector(disk, db);
        inode_free(disk, ci);
        if (new_domain_id != DOMAIN_SLOT_FREE && new_domain_id != 0)
            domain_free(disk, new_domain_id);
        return false;
    }
    cie.start_block = db;
    cie.domain_id   = parent_domain;
    memset(cie.resource_tag, 0, 8);
    strncpy(cie.resource_tag, new_tag.c_str(), 7);

    if (!inode_write(disk, ci, cie)) {
        free_sector(disk, db);
        inode_free(disk, ci);
        if (new_domain_id != DOMAIN_SLOT_FREE && new_domain_id != 0)
            domain_free(disk, new_domain_id);
        return false;
    }

    uint8_t empty[BLOCK_SIZE] = {0};
    disk->write_sector(db, empty);

    if (insert_into_dir(disk, parent_inode, name, ci, 0, new_tag, db) == 0) {
        free_sector(disk, db);
        inode_free(disk, ci);
        if (new_domain_id != DOMAIN_SLOT_FREE && new_domain_id != 0)
            domain_free(disk, new_domain_id);
        return false;
    }
    return true;
}

// ============================================================
// write_file —— COW + 完整回滚 + 定长索引适配
// ============================================================
bool DiskFileSystem::write_file(BlockDevice* disk, const std::string& vfs_path,
                                const std::string& content) {
    if (!disk) return false;
    DiskScope scope(disk);

    uint32_t inode_id = path_to_inode(disk, vfs_path);
    if (inode_id == 0) return false;

    InodeEntry ie_old;
    if (!inode_read(disk, inode_id, ie_old)) return false;
    if (ie_old.type != 0) return false;

    if (!journal_begin(disk, vfs_path, "", 0)) return false;

    // ---- 步骤 1：分配新块 + 写数据 ----
    size_t total = content.size();
    size_t chunks = (total + FILE_DATA_CHUNK - 1) / FILE_DATA_CHUNK;
    if (chunks == 0) chunks = 1;

    std::vector<uint32_t> new_blocks;
    new_blocks.reserve(chunks);
    for (size_t i = 0; i < chunks; ++i) {
        uint32_t b = allocate_sector(disk);
        if (b == 0) {
            for (auto x : new_blocks) free_sector(disk, x);
            journal_abort(disk);
            return false;
        }
        new_blocks.push_back(b);
    }

    for (size_t i = 0; i < chunks; ++i) {
        uint8_t buf[BLOCK_SIZE] = {0};
        size_t offset = i * FILE_DATA_CHUNK;
        if (offset < total) {
            size_t len = std::min((size_t)FILE_DATA_CHUNK, total - offset);
            memcpy(buf, content.data() + offset, len);
        }
        uint32_t next = (i + 1 < chunks) ? new_blocks[i + 1] : 0;
        memcpy(buf + FILE_DATA_CHUNK, &next, 4);
        if (!disk->write_sector(new_blocks[i], buf)) {
            for (auto x : new_blocks) free_sector(disk, x);
            journal_abort(disk);
            return false;
        }
    }

    // ---- 步骤 2：准备新 inode ----
    InodeEntry ie_new = ie_old;
    ie_new.start_block  = new_blocks[0];
    ie_new.size         = total;
    ie_new.block_count  = (uint32_t)chunks;
    ie_new.indirect_block = 0; 

    // ---- 步骤 3：更新 inode ----
    if (!inode_write(disk, inode_id, ie_new)) {
        for (auto x : new_blocks) free_sector(disk, x);
        journal_abort(disk);
        return false;
    }

    // ---- 步骤 4：更新父目录项（适配定长结构） ----
    uint32_t parent_inode = path_to_inode(disk, get_parent_path(vfs_path));
    std::string name = get_filename(vfs_path);
    
    if (parent_inode == 0 || name.empty()) {
        inode_write(disk, inode_id, ie_old);
        for (auto x : new_blocks) free_sector(disk, x);
        journal_abort(disk);
        return false;
    }

    InodeEntry pie;
    if (!inode_read(disk, parent_inode, pie)) {
        inode_write(disk, inode_id, ie_old);
        for (auto x : new_blocks) free_sector(disk, x);
        journal_abort(disk);
        return false;
    }

    uint32_t scan = pie.start_block;
    std::set<uint32_t> visited_dir;
    bool updated = false;

    while (scan != 0 && is_block_valid(disk, scan) && !visited_dir.count(scan)) {
        visited_dir.insert(scan);

        uint8_t buf[BLOCK_SIZE] = {0};
        if (!disk->read_sector(scan, buf)) break;

        uint32_t next_block = 0;
        bool advanced = false;

        // 🚀 核心修复：定长步进遍历
        for (size_t offset = 0; offset + sizeof(DirEntryHeader) <= BLOCK_SIZE; offset += sizeof(DirEntryHeader)) {
            DirEntryHeader* hdr = reinterpret_cast<DirEntryHeader*>(buf + offset);

            // 处理 __NEXT__
            if (hdr->inode_id == 0xFFFFFFFEu) {
                next_block = hdr->start_sector;
                advanced = true;
                break;
            }
            
            // 空槽/墓碑
            if (hdr->inode_id == 0) continue;

            // 去名字块比对
            std::string cur = read_name_from_blocks(disk, hdr->name_block, hdr->name_len);
            if (cur == name) {
                hdr->size         = total;
                hdr->start_sector = ie_new.start_block;
                if (disk->write_sector(scan, buf)) updated = true;
                break;
            }
        }
        
        if (updated) break;
        if (advanced && next_block != 0) {
            scan = next_block;
            continue;
        }
        break; // 既没找到，也没有下一块了
    }

    if (!updated) {
        // 🚀 回滚 inode 到旧值
        inode_write(disk, inode_id, ie_old);
        for (auto x : new_blocks) free_sector(disk, x);
        journal_abort(disk);
        return false;
    }

    // ---- 步骤 5：成功 → 释放旧数据链 ----
    file_free_old_data_chain(disk, ie_old);

    journal_commit(disk);
    verify_write_backup(disk);
    hidden_write_backup(disk);
    return true;
}

// ============================================================
// read_file_content
// ============================================================
std::string DiskFileSystem::read_file_content(BlockDevice* disk, const std::string& vfs_path) {
    if (!disk) return "";
    DiskScope scope(disk);

    uint32_t inode_id = path_to_inode(disk, vfs_path);
    if (inode_id == 0) return "";

    InodeEntry ie;
    if (!inode_read(disk, inode_id, ie)) return "";
    if (ie.type != 0) return "";

    size_t max_possible = (size_t)total_blocks_of(disk) * FILE_DATA_CHUNK;
    size_t want_size    = (size_t)std::min<uint64_t>(ie.size, (uint64_t)max_possible);

    std::string result;
    result.reserve(want_size);

    uint32_t b = ie.start_block;
    uint64_t remaining = want_size;
    std::set<uint32_t> visited_read;

    while (b != 0 && remaining > 0 && is_block_valid(disk, b)) {
        if (!visited_read.insert(b).second) {
            std::cerr << "[READ] 文件块环，中断: " << vfs_path << "\n";
            break;
        }

        uint8_t buf[BLOCK_SIZE] = {0};
        if (!disk->read_sector(b, buf)) break;

        size_t chunk = std::min((size_t)FILE_DATA_CHUNK, (size_t)remaining);
        result.append(reinterpret_cast<const char*>(buf), chunk);
        remaining -= chunk;

        if (remaining == 0) break;
        uint32_t next = 0;
        std::memcpy(&next, buf + FILE_DATA_CHUNK, 4);
        b = next;
    }

    if (remaining > 0)
        std::cerr << "[READ] 文件读未完成，可能已损坏: " << vfs_path << "\n";

    return result;
}