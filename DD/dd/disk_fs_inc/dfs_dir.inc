// ============================================================
// dfs_dir.inc —— 目录操作 v7（名字分离架构 + 定长遍历 + 幽灵过滤）
// ============================================================

// ---------- 共享 helper ----------
struct RawDirEntry {
    DirEntryHeader hdr;
    std::string    name;
};

// ============================================================
// 🚀 名字块读写逻辑（专门放名字的区域）
// ============================================================
static uint32_t write_name_to_blocks(BlockDevice* disk, const std::string& name) {
    if (name.empty()) return 0;
    
    std::string remaining = name;
    uint32_t first_block = 0;
    uint32_t prev_block = 0;

    while (!remaining.empty()) {
        uint32_t nb = DiskFileSystem::allocate_sector(disk);
        if (nb == 0) {
            std::cerr << "[DEBUG_WRITE_NAME] allocate_sector 失败，磁盘可能已满\n";
            break;
        }

        if (first_block == 0) first_block = nb;

        uint8_t buf[BLOCK_SIZE] = {0};
        NameBlockHeader* nbh = reinterpret_cast<NameBlockHeader*>(buf);
        nbh->magic = NAME_BLOCK_MAGIC;
        nbh->next_block = 0;
        
        size_t space = BLOCK_SIZE - sizeof(NameBlockHeader);
        size_t take = std::min(space, remaining.size());
        
        nbh->this_len = (uint16_t)take;
        std::memcpy(buf + sizeof(NameBlockHeader), remaining.data(), take);
        remaining = remaining.substr(take);

        if (prev_block != 0) {
            uint8_t pbuf[BLOCK_SIZE] = {0};
            disk->read_sector(prev_block, pbuf);
            NameBlockHeader* pnbh = reinterpret_cast<NameBlockHeader*>(pbuf);
            pnbh->next_block = nb;
            disk->write_sector(prev_block, pbuf);
        }

        if (!disk->write_sector(nb, buf)) {
            std::cerr << "[DEBUG_WRITE_NAME] 写入名字块失败，sector=" << nb << "\n";
            break;
        }
        prev_block = nb;
    }
    return first_block;
}

static std::string read_name_from_blocks(BlockDevice* disk, uint32_t first_block, uint16_t name_len) {
    if (first_block == 0 || name_len == 0) return "";
    std::string result;
    result.reserve(name_len);

    uint32_t scan = first_block;
    std::set<uint32_t> visited;
    while (scan != 0 && result.size() < name_len && !visited.count(scan)) {
        visited.insert(scan);
        uint8_t buf[BLOCK_SIZE] = {0};
        if (!disk->read_sector(scan, buf)) break;
        NameBlockHeader* nbh = reinterpret_cast<NameBlockHeader*>(buf);
        if (nbh->magic != NAME_BLOCK_MAGIC) break;
        
        result.append(reinterpret_cast<const char*>(buf + sizeof(NameBlockHeader)), nbh->this_len);
        scan = nbh->next_block;
    }
    if (result.size() > name_len) result.resize(name_len);
    return result;
}

static void free_name_blocks(BlockDevice* disk, uint32_t first_block) {
    uint32_t scan = first_block;
    std::set<uint32_t> visited;
    while (scan != 0 && !visited.count(scan)) {
        visited.insert(scan);
        uint8_t buf[BLOCK_SIZE] = {0};
        if (!disk->read_sector(scan, buf)) break;
        NameBlockHeader* nbh = reinterpret_cast<NameBlockHeader*>(buf);
        uint32_t next = nbh->next_block;
        DiskFileSystem::free_sector(disk, scan);
        scan = next;
    }
}

// ============================================================
// 🚀 读取目录项
// ============================================================
static bool read_dir_entries_unlocked(BlockDevice* disk, uint32_t dir_block,
                                      std::vector<RawDirEntry>& out) {
    out.clear();
    uint32_t scan = dir_block;
    std::set<uint32_t> visited;

    while (scan != 0 && is_block_valid(disk, scan) && !visited.count(scan)) {
        visited.insert(scan);
        uint8_t buf[BLOCK_SIZE] = {0};
        if (!disk->read_sector(scan, buf)) return false;

        uint32_t next_block = 0;
        // 定长遍历
        for (size_t offset = 0; offset + sizeof(DirEntryHeader) <= BLOCK_SIZE; offset += sizeof(DirEntryHeader)) {
            DirEntryHeader* hdr = reinterpret_cast<DirEntryHeader*>(buf + offset);
            
            if (hdr->inode_id == 0xFFFFFFFEu) {
                next_block = hdr->start_sector;
                break; // __NEXT__ 是本块的最后一个元素
            }
            if (hdr->inode_id == 0) continue; // 墓碑/空

            std::string nm = read_name_from_blocks(disk, hdr->name_block, hdr->name_len);
            
            // 🚀 核心过滤 1：如果名字读取失败或者为空，说明该条目是幽灵，直接丢弃
            if (nm.empty()) continue;

            RawDirEntry e;
            e.hdr  = *hdr;
            e.name = nm;
            out.push_back(std::move(e));
        }

        if (next_block == 0) break;
        scan = next_block;
    }
    return true;
}

// ============================================================
// 🚀 重写单块（用于压缩/重命名场景，只写索引）
// ============================================================
static bool write_dir_entries_single_block_unlocked(BlockDevice* disk, uint32_t dir_block,
                                                     const std::vector<RawDirEntry>& entries) {
    uint8_t buf[BLOCK_SIZE] = {0};
    size_t  offset = 0;
    for (const auto& e : entries) {
        if (offset + sizeof(DirEntryHeader) > BLOCK_SIZE) return false;
        std::memcpy(buf + offset, &e.hdr, sizeof(DirEntryHeader));
        offset += sizeof(DirEntryHeader);
    }
    return disk->write_sector(dir_block, buf);
}

// ============================================================
// 内部：目录查找
// ============================================================
static uint32_t find_inode_in_dir(BlockDevice* disk, uint32_t dir_inode, const std::string& name) {
    if (name.empty()) return 0;
    DiskScope scope(disk);

    InodeEntry dir_ie;
    if (!DiskFileSystem::inode_read(disk, dir_inode, dir_ie)) return 0;
    if (dir_ie.type != 1) return 0;

    std::vector<RawDirEntry> entries;
    if (!read_dir_entries_unlocked(disk, dir_ie.start_block, entries)) return 0;
    for (const auto& e : entries)
        if (e.name == name) return e.hdr.inode_id;
    return 0;
}

static uint32_t path_to_inode(BlockDevice* disk, const std::string& vfs_path) {
    if (vfs_path.empty() || vfs_path == "/") return 2;
    DiskScope scope(disk);

    std::stringstream ss(vfs_path);
    std::string token;
    std::vector<std::string> parts;
    while (std::getline(ss, token, '/'))
        if (!token.empty()) parts.push_back(token);

    uint32_t cur = 2;
    for (const auto& p : parts) {
        cur = find_inode_in_dir(disk, cur, p);
        if (cur == 0) return 0;
    }
    return cur;
}

// ============================================================
// 🚀 insert_into_dir —— 定长索引插入（永不爆块）
// ============================================================
static uint32_t insert_into_dir(BlockDevice* disk, uint32_t parent_inode,
                                 const std::string& name, uint32_t child_inode, uint32_t type,
                                 const std::string& tag, uint32_t data_block) {
    if (!disk || name.empty()) return 0;
    DiskScope scope(disk);

    InodeEntry pie;
    if (!DiskFileSystem::inode_read(disk, parent_inode, pie)) {
        std::cerr << "[DEBUG_INSERT] 读取父 inode 失败\n";
        return 0;
    }
    if (pie.type != 1) {
        std::cerr << "[DEBUG_INSERT] 父 inode 不是目录\n";
        return 0;
    }
    uint32_t parent_domain = pie.domain_id;

    // 🚀 1. 先把名字写入名字块
    uint32_t name_blk = write_name_to_blocks(disk, name);
    if (name_blk == 0) {
        std::cerr << "[DEBUG_INSERT] 写入名字块失败\n";
        return 0;
    }

    uint32_t scan = pie.start_block;
    if (scan == 0) {
        std::cerr << "[DEBUG_INSERT] 🚨 父目录的 start_block 为 0！根目录数据块丢失！\n";
        free_name_blocks(disk, name_blk);
        return 0;
    }

    std::set<uint32_t> visited;

    while (scan != 0 && is_block_valid(disk, scan) && !visited.count(scan)) {
        visited.insert(scan);
        uint8_t buf[BLOCK_SIZE] = {0};
        if (!disk->read_sector(scan, buf)) {
            std::cerr << "[DEBUG_INSERT] 读取父目录块失败 sector=" << scan << "\n";
            free_name_blocks(disk, name_blk);
            return 0;
        }

        uint32_t next_block = 0;
        size_t   free_slot  = BLOCK_SIZE; // 默认没有空位
        size_t   used_end   = 0;
        bool     conflict   = false;

        for (size_t offset = 0; offset + sizeof(DirEntryHeader) <= BLOCK_SIZE; offset += sizeof(DirEntryHeader)) {
            DirEntryHeader* hdr = reinterpret_cast<DirEntryHeader*>(buf + offset);
            
            if (hdr->inode_id == 0xFFFFFFFEu) {
                next_block = hdr->start_sector;
                used_end = offset + sizeof(DirEntryHeader);
                break;
            }
            if (hdr->inode_id == 0) {
                if (free_slot == BLOCK_SIZE) free_slot = offset; // 记录可复用的墓碑
            } else {
                // 冲突检查
                std::string ex_name = read_name_from_blocks(disk, hdr->name_block, hdr->name_len);
                if (ex_name == name) { conflict = true; break; }
                used_end = offset + sizeof(DirEntryHeader);
            }
        }

        if (conflict) {
            std::cerr << "[DEBUG_INSERT] 目录项冲突，名字已存在\n";
            free_name_blocks(disk, name_blk);
            return 0;
        }

        // 优先填补墓碑空位；如果没有，就追加到末尾
        size_t target_offset = (free_slot != BLOCK_SIZE) ? free_slot : used_end;
        
        // 如果填得下当前条目，并且不会挤压 __NEXT__ 的空间（保留 40 字节）
        if (target_offset + sizeof(DirEntryHeader) + sizeof(DirEntryHeader) <= BLOCK_SIZE || next_block != 0) {
            DirEntryHeader* hdr = reinterpret_cast<DirEntryHeader*>(buf + target_offset);
            std::memset(hdr, 0, sizeof(*hdr));
            hdr->inode_id      = child_inode;
            hdr->type          = type;
            hdr->start_sector  = data_block;
            hdr->parent_sector = scan;
            hdr->size          = 0;
            hdr->name_block    = name_blk;
            hdr->name_len      = (uint16_t)name.size();
            std::strncpy(hdr->resource_tag, tag.c_str(), 7);

            if (!disk->write_sector(scan, buf)) {
                std::cerr << "[DEBUG_INSERT] 写回父目录块失败 sector=" << scan << "\n";
                free_name_blocks(disk, name_blk);
                return 0;
            }

            if (!DiskFileSystem::link_alloc(disk, parent_inode, child_inode, parent_domain, name)) {
                std::cerr << "[DEBUG_INSERT] link_alloc 失败\n";
                std::memset(hdr, 0, sizeof(*hdr));
                disk->write_sector(scan, buf);
                free_name_blocks(disk, name_blk);
                return 0;
            }
            return scan;
        }

        // 如果本块满了，去下一块
        if (next_block != 0) {
            scan = next_block;
            continue;
        }

        // ============================================================
        // 🚀 如果末尾块真的一点缝都没了，分配新块
        // ============================================================
        uint32_t nb = DiskFileSystem::allocate_sector(disk);
        if (nb == 0) {
            std::cerr << "[DEBUG_INSERT] 分配新块失败，磁盘满\n";
            free_name_blocks(disk, name_blk);
            return 0;
        }

        if (used_end + sizeof(DirEntryHeader) > BLOCK_SIZE) {
            DiskFileSystem::free_sector(disk, nb);
            free_name_blocks(disk, name_blk);
            return 0;
        }
        DirEntryHeader* nx = reinterpret_cast<DirEntryHeader*>(buf + used_end);
        std::memset(nx, 0, sizeof(*nx));
        nx->inode_id     = 0xFFFFFFFEu;
        nx->start_sector = nb;

        if (!disk->write_sector(scan, buf)) {
            DiskFileSystem::free_sector(disk, nb);
            free_name_blocks(disk, name_blk);
            return 0;
        }

        // 在新块里写索引
        uint8_t nbuf[BLOCK_SIZE] = {0};
        DirEntryHeader* nhdr = reinterpret_cast<DirEntryHeader*>(nbuf);
        nhdr->inode_id      = child_inode;
        nhdr->type          = type;
        nhdr->start_sector  = data_block;
        nhdr->parent_sector = nb;
        nhdr->size          = 0;
        nhdr->name_block    = name_blk;
        nhdr->name_len      = (uint16_t)name.size();
        std::strncpy(nhdr->resource_tag, tag.c_str(), 7);

        if (!disk->write_sector(nb, nbuf)) {
            std::memset(buf + used_end, 0, sizeof(DirEntryHeader));
            disk->write_sector(scan, buf);
            DiskFileSystem::free_sector(disk, nb);
            free_name_blocks(disk, name_blk);
            return 0;
        }

        if (!DiskFileSystem::link_alloc(disk, parent_inode, child_inode, parent_domain, name)) {
            DiskFileSystem::free_sector(disk, nb);
            std::memset(buf + used_end, 0, sizeof(DirEntryHeader));
            disk->write_sector(scan, buf);
            free_name_blocks(disk, name_blk);
            return 0;
        }
        return nb;
    }
    return 0;
}

// ============================================================
// find_entry
// ============================================================
bool DiskFileSystem::find_entry(BlockDevice* disk, const std::string& vfs_path,
                                DirEntry& out_entry) {
    if (!disk) return false;
    DiskScope scope(disk);

    std::string name = get_filename(vfs_path);
    if (name.empty() || name == "/") return false;

    uint32_t parent_inode = path_to_inode(disk, get_parent_path(vfs_path));
    if (parent_inode == 0) return false;

    uint32_t child_inode = find_inode_in_dir(disk, parent_inode, name);
    if (child_inode == 0) return false;

    InodeEntry ie;
    if (!DiskFileSystem::inode_read(disk, child_inode, ie)) return false;

    std::memset(&out_entry, 0, sizeof(out_entry));
    size_t n = std::min(name.size(), (size_t)MAX_NAME_LEN);
    std::memcpy(out_entry.name, name.data(), n);
    out_entry.name[n]        = '\0';
    out_entry.inode_id       = child_inode;
    out_entry.type           = ie.type;
    out_entry.start_sector   = ie.start_block;
    out_entry.size           = ie.size;
    out_entry.parent_sector  = ie.parent_inode;
    std::memcpy(out_entry.resource_tag, ie.resource_tag, 8);
    return true;
}

// ============================================================
// create_directory (植入全量探针)
// ============================================================
bool DiskFileSystem::create_directory(BlockDevice* disk, const std::string& vfs_path) {
    if (!disk) return false;
    DiskScope scope(disk);

    std::string name = get_filename(vfs_path);
    if (name.empty() || name.size() > LINK_NAME_CAP || name == "/") {
        return false;
    }

    std::string parent_path = get_parent_path(vfs_path);
    uint32_t parent_inode = path_to_inode(disk, parent_path);
    if (parent_inode == 0) {
        return false;
    }
    if (find_inode_in_dir(disk, parent_inode, name) != 0) {
        return false;
    }

    InodeEntry pie;
    if (!DiskFileSystem::inode_read(disk, parent_inode, pie)) {
        return false;
    }

    std::string new_tag    = "CMD";
    uint32_t    parent_domain = pie.domain_id;
    uint32_t    new_domain_id = DOMAIN_SLOT_FREE;

    if (pie.resource_tag[0] != '\0') {
        new_tag = std::string(pie.resource_tag, strnlen(pie.resource_tag, 8));
    } else {
        new_domain_id = DiskFileSystem::domain_alloc(disk, parent_domain, name);
        if (new_domain_id != DOMAIN_SLOT_FREE && new_domain_id != 0) {
            parent_domain = new_domain_id;
            new_tag = "D" + std::to_string(new_domain_id);
        }
    }

    uint32_t ci = DiskFileSystem::inode_alloc(disk, 1, parent_inode, parent_domain);
    if (ci == 0) {
        if (new_domain_id != DOMAIN_SLOT_FREE && new_domain_id != 0)
            DiskFileSystem::domain_free(disk, new_domain_id);
        return false;
    }

    uint32_t db = DiskFileSystem::allocate_sector(disk);
    if (db == 0) {
        DiskFileSystem::inode_free(disk, ci);
        if (new_domain_id != DOMAIN_SLOT_FREE && new_domain_id != 0)
            DiskFileSystem::domain_free(disk, new_domain_id);
        return false;
    }

    InodeEntry cie;
    if (!DiskFileSystem::inode_read(disk, ci, cie)) {
        DiskFileSystem::free_sector(disk, db);
        DiskFileSystem::inode_free(disk, ci);
        if (new_domain_id != DOMAIN_SLOT_FREE && new_domain_id != 0)
            DiskFileSystem::domain_free(disk, new_domain_id);
        return false;
    }
    cie.start_block = db;
    cie.domain_id   = parent_domain;
    std::memset(cie.resource_tag, 0, 8);
    std::strncpy(cie.resource_tag, new_tag.c_str(), 7);
    if (!DiskFileSystem::inode_write(disk, ci, cie)) {
        DiskFileSystem::free_sector(disk, db);
        DiskFileSystem::inode_free(disk, ci);
        if (new_domain_id != DOMAIN_SLOT_FREE && new_domain_id != 0)
            DiskFileSystem::domain_free(disk, new_domain_id);
        return false;
    }

    uint8_t empty[BLOCK_SIZE] = {0};
    disk->write_sector(db, empty);

    uint32_t insert_res = insert_into_dir(disk, parent_inode, name, ci, 1, new_tag, db);
    if (insert_res == 0) {
        DiskFileSystem::free_sector(disk, db);
        DiskFileSystem::inode_free(disk, ci);
        if (new_domain_id != DOMAIN_SLOT_FREE && new_domain_id != 0)
            DiskFileSystem::domain_free(disk, new_domain_id);
        return false;
    }

    return true;
}

// ============================================================
// delete_entry —— 递归删目录 + 释放名字块
// ============================================================
bool DiskFileSystem::delete_entry(BlockDevice* disk, const std::string& vfs_path) {
    if (!disk) return false;
    DiskScope scope(disk);

    if (vfs_path.empty() || vfs_path == "/") return false;

    uint32_t inode_id = path_to_inode(disk, vfs_path);
    if (inode_id == 0 || inode_id < 2) return false;

    InodeEntry ie;
    if (!DiskFileSystem::inode_read(disk, inode_id, ie)) return false;

    if (ie.type == 1) {
        std::vector<RawDirEntry> children;
        if (read_dir_entries_unlocked(disk, ie.start_block, children)) {
            for (const auto& child : children) {
                std::string cp = vfs_path;
                if (cp.empty() || cp.back() != '/') cp += "/";
                cp += child.name;
                delete_entry(disk, cp);
            }
        }
    }

    uint32_t parent_inode = path_to_inode(disk, get_parent_path(vfs_path));
    if (parent_inode == 0) return false;

    InodeEntry pie;
    if (DiskFileSystem::inode_read(disk, parent_inode, pie)) {
        std::string name = get_filename(vfs_path);
        uint32_t scan = pie.start_block;
        std::set<uint32_t> visited;
        bool removed = false;

        while (scan != 0 && is_block_valid(disk, scan) && !visited.count(scan) && !removed) {
            visited.insert(scan);
            uint8_t buf[BLOCK_SIZE] = {0};
            if (!disk->read_sector(scan, buf)) break;

            uint32_t next_block = 0;
            for (size_t offset = 0; offset + sizeof(DirEntryHeader) <= BLOCK_SIZE; offset += sizeof(DirEntryHeader)) {
                DirEntryHeader* hdr = reinterpret_cast<DirEntryHeader*>(buf + offset);
                
                if (hdr->inode_id == 0xFFFFFFFEu) {
                    next_block = hdr->start_sector;
                    break;
                }
                if (hdr->inode_id == 0) continue;

                std::string nm = read_name_from_blocks(disk, hdr->name_block, hdr->name_len);
                if (nm == name) {
                    // 🚀 释放名字块
                    free_name_blocks(disk, hdr->name_block);
                    // 留墓碑
                    std::memset(buf + offset, 0, sizeof(DirEntryHeader));
                    disk->write_sector(scan, buf);
                    removed = true;
                    break;
                }
            }
            if (removed) break;
            if (next_block == 0) break;
            scan = next_block;
        }
    }

    DiskFileSystem::link_free_by_nodes(disk, parent_inode, inode_id);
    DiskFileSystem::inode_free(disk, inode_id);
    return true;
}

// ============================================================
// list_dir
// ============================================================
std::vector<std::string> DiskFileSystem::list_dir(BlockDevice* disk, const std::string& vfs_path) {
    std::vector<std::string> result;
    if (!disk) return result;
    DiskScope scope(disk);

    if (vfs_path == "/dev") return result;

    uint32_t inode_id = path_to_inode(disk, vfs_path);
    if (inode_id == 0) return result;

    InodeEntry ie;
    if (!DiskFileSystem::inode_read(disk, inode_id, ie)) return result;
    if (ie.type != 1) return result;

    std::vector<RawDirEntry> entries;
    if (!read_dir_entries_unlocked(disk, ie.start_block, entries)) return result;

    for (const auto& e : entries) {
        // 🚀 核心过滤 2：在输出层再次过滤空名字，防止底层漏网之鱼
        if (e.name.empty()) continue;

        std::string prefix = (e.hdr.type == 1) ? "[目录] " : "[文件] ";
        result.push_back(prefix + e.name);
    }
    return result;
}