#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
#include <utility>

class BlockDevice;

// ==========================================================
// 🚀 核心块大小
// ==========================================================
inline constexpr uint32_t BLOCK_SIZE        = 4096;
inline constexpr uint32_t BLOCK_HEADER_SIZE = 96;
inline constexpr uint32_t FILE_DATA_CHUNK   = BLOCK_SIZE - BLOCK_HEADER_SIZE;  // 4000

// 🚀 名字长度上限
// 现在名字单独存放在名字块中，这个上限只用于内存缓冲，理论上可以无限长（受 uint16_t 限制）
inline constexpr uint16_t MAX_NAME_LEN = 1024;

// 🚀 无效扇区/块
inline constexpr uint32_t INVALID_SECTOR = 0xFFFFFFFFu;

// ==========================================================
// 🚀 末尾保留区
// ==========================================================
inline constexpr uint32_t RESERVED_BLOCKS = 8;
inline constexpr uint32_t LOG_BLOCKS      = 3;
inline constexpr uint32_t VERIFY_BLOCKS   = 2;
inline constexpr uint32_t HIDDEN_BLOCKS   = 3;
static_assert(LOG_BLOCKS + VERIFY_BLOCKS + HIDDEN_BLOCKS == RESERVED_BLOCKS,
              "末尾保留区子块之和必须等于 RESERVED_BLOCKS！");

// ==========================================================
// 🚀 固定元数据块布局
//   0      : Superblock
//   1      : PhysicalLogicalMap
//   2..5   : ScopeTableHeader + ScopeEntry (4块)
//   6      : 预留
//   7..10  : VirtualBitmapHeader + Node (4块)
//   11     : 预留
//   12..19 : PhysicalLogicalHeader[0..7] (8块)
//   20..35 : InodeTable (16块)
//   36..37 : DomainTable (2块)
//   38..69 : LinkTable (32块)
//   70     : data_start == root_dir_start
// ==========================================================
inline constexpr uint32_t SUPERBLOCK_BLOCK      = 0;
inline constexpr uint32_t PARTITION_TABLE_BLOCK = 1;

inline constexpr uint32_t SCOPE_TABLE_BLOCK        = 2;
inline constexpr uint32_t SCOPE_TABLE_BLOCKS_DEF   = 4;    // 2..5

inline constexpr uint32_t VBITMAP_TABLE_BLOCK      = 7;
inline constexpr uint32_t VBITMAP_TABLE_BLOCKS_DEF = 4;    // 7..10

inline constexpr uint32_t PLH_TABLE_BLOCK          = 12;   // 12..19 (MAX_PARTITIONS=8)

inline constexpr uint32_t INODE_TABLE_START_BLOCK_DEF = 20;
inline constexpr uint32_t INODE_TABLE_BLOCKS_DEF      = 16;   // 20..35
inline constexpr uint32_t DOMAIN_TABLE_START_BLOCK_DEF = 36;
inline constexpr uint32_t DOMAIN_TABLE_BLOCKS_DEF      = 2;   // 36..37

inline constexpr uint32_t LINK_TABLE_START_BLOCK_DEF   = 38;
inline constexpr uint32_t LINK_TABLE_BLOCKS_DEF        = 32;  // 38..69

// 🚀 自适应推算 data_start，彻底避免手动改大小导致的物理重叠！
inline constexpr uint32_t DATA_START_BLOCK_DEF = LINK_TABLE_START_BLOCK_DEF + LINK_TABLE_BLOCKS_DEF;

inline constexpr uint32_t METADATA_FIXED_END = DATA_START_BLOCK_DEF;
inline constexpr uint32_t METADATA_FALLBACK_START = DATA_START_BLOCK_DEF;

// ==========================================================
// 魔数
// ==========================================================
inline constexpr uint32_t SUPER_MAGIC     = 0x5859;
inline constexpr uint32_t BLOCK_MAGIC     = 0x424C4B48;
inline constexpr uint32_t INODE_MAGIC     = 0x494E4F44;
inline constexpr uint32_t INDEX_MAGIC     = 0x494E4442;
inline constexpr uint32_t DOMAIN_MAGIC    = 0x444F4D54;
inline constexpr uint32_t LINK_MAGIC      = 0x4C4E4B54;
inline constexpr uint32_t JOURNAL_MAGIC   = 0x4A524E4C;
inline constexpr uint32_t VERIFY_MAGIC    = 0x56455249;
inline constexpr uint32_t HIDDEN_MAGIC    = 0x4844454E;
inline constexpr uint32_t PART_MAGIC      = 0x50415254;

inline constexpr uint32_t SCOPE_MAGIC     = 0x53434F50;
inline constexpr uint32_t VBITMAP_MAGIC   = 0x56424D50;
inline constexpr uint32_t STATIC_MAGIC    = 0x53544344;
inline constexpr uint32_t DYN_HEAD_MAGIC  = 0x44594E48;
inline constexpr uint32_t PLH_MAGIC       = 0x504C4844;
inline constexpr uint32_t NAME_BLOCK_MAGIC = 0x4E414D45; // 🚀 "NAME"

inline constexpr const char* RES_TAG_R0   = "R0";
inline constexpr const char* RES_TAG_HOME = "HOME";
inline constexpr const char* RES_TAG_ROOT = "ROOT";
inline constexpr const char* RES_TAG_CMD  = "CMD";
inline constexpr const char* RES_TAG_PATH = "PATH";

// ==========================================================
// 索引块
// ==========================================================
inline constexpr uint32_t INDEX_PTR_MAX = (BLOCK_SIZE - 8) / 4;

#pragma pack(push, 1)
struct IndexBlock {
    uint32_t magic;
    uint32_t count;
    uint32_t pointers[INDEX_PTR_MAX];
};
#pragma pack(pop)
static_assert(sizeof(IndexBlock) == BLOCK_SIZE, "IndexBlock 大小必须严格等于 4096 字节！");

// ==========================================================
// 🚀 块头
// ==========================================================
#pragma pack(push, 1)
struct BlockHeader {
    uint32_t magic;
    uint32_t inode_id;
    uint32_t block_index;
    uint32_t next_block;
    uint32_t ext_block;
    uint32_t data_size;
    uint32_t checksum;
    uint32_t flags;
    uint8_t  reserved[64];
};
#pragma pack(pop)
static_assert(sizeof(BlockHeader) == BLOCK_HEADER_SIZE, "BlockHeader 大小必须严格等于 96 字节！");

// ==========================================================
// 🚀 链接表（磁盘定长 + 内存变长）
// ==========================================================
inline constexpr uint32_t LINK_NAME_CAP  = 128;          
inline constexpr uint32_t LINK_SLOT_FREE = 0xFFFFFFFFu;  

#pragma pack(push, 1)
struct LinkEntryDisk {
    uint32_t link_id;
    uint32_t parent_inode;
    uint32_t child_inode;
    uint32_t domain_id;
    uint32_t flags;
    uint16_t name_len;         
    char     name[LINK_NAME_CAP]; 
};
#pragma pack(pop)
static_assert(sizeof(LinkEntryDisk) == 150, "LinkEntryDisk 必须严格等于 150 字节！");
inline constexpr int LINKS_PER_BLOCK = BLOCK_SIZE / sizeof(LinkEntryDisk);

struct LinkEntry {
    uint32_t link_id;
    uint32_t parent_inode;
    uint32_t child_inode;
    uint32_t domain_id;
    std::string name;
    uint32_t flags;
};

// ==========================================================
// 🚀 inode 条目
// ==========================================================
#pragma pack(push, 1)
struct InodeEntry {
    uint32_t inode_id;
    uint32_t type;
    uint32_t start_block;

    union {
        uint32_t indirect_block;  
        uint32_t ext_block;       
    };

    uint64_t size;
    uint32_t block_count;
    uint32_t parent_inode;
    uint32_t domain_id;
    uint32_t link_count;
    uint32_t owner_id;
    char     resource_tag[8];
    uint32_t reserved[3];
};
#pragma pack(pop)
inline constexpr uint32_t INODE_ENTRY_SIZE = sizeof(InodeEntry);
inline constexpr uint32_t INODE_PER_BLOCK  = BLOCK_SIZE / sizeof(InodeEntry);

// ==========================================================
// 🚀 域表条目
// ==========================================================
inline constexpr uint32_t DOMAIN_SLOT_FREE = 0xFFFFFFFFu; 

#pragma pack(push, 1)
struct DomainEntry {
    uint32_t domain_id;
    uint32_t parent_id;
    uint32_t inode_start;
    uint32_t inode_count;
    uint32_t data_start;
    uint32_t data_end;
    char     name[32];
    uint32_t reserved[2];
};
#pragma pack(pop)
inline constexpr uint32_t DOMAIN_ENTRY_SIZE = sizeof(DomainEntry);
inline constexpr uint32_t DOMAIN_PER_BLOCK  = BLOCK_SIZE / sizeof(DomainEntry);

#pragma pack(push, 1)
struct DomainTableHeader {
    uint32_t magic;
    uint32_t domain_count;
    uint32_t next_table_block;
    uint32_t reserved;
};
#pragma pack(pop)

// ==========================================================
// 日志 / 验证 / 隐藏
// ==========================================================
#pragma pack(push, 1)
struct JournalHeader {
    uint32_t magic;
    uint32_t txn_id;
    uint32_t state;
    uint32_t data_size;
    uint32_t target_block;
    char     target_path[64];
    uint32_t checksum;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct VerifyHeader {
    uint32_t magic;
    uint32_t sb_checksum;      
    uint32_t bitmap_blocks;
    uint32_t total_files;
    uint32_t total_dirs;
    uint32_t reserved;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct HiddenHeader {
    uint32_t magic;
    uint32_t sb_backup_size;
    uint32_t root_snapshot_size;
    uint32_t timestamp;
};
#pragma pack(pop)

// ==========================================================
// 🚀 名字块 (Name Block) —— 专门存放超长名字的区域
// ==========================================================
#pragma pack(push, 1)
struct NameBlockHeader {
    uint32_t magic;          // NAME_BLOCK_MAGIC
    uint32_t next_block;     // 🚀 指向下一个名字块（如果名字特别长）
    uint16_t this_len;       // 当前块里存了多少字节的名字
    uint16_t reserved;
    // 后面紧跟真实的变长名字数据，不超过 (BLOCK_SIZE - 12) 字节
};
#pragma pack(pop)

// ==========================================================
// 🚀 DirEntryHeader (固定长度索引，不含名字实体)
// ==========================================================
#pragma pack(push, 1)
struct DirEntryHeader {
    uint32_t inode_id;
    uint32_t type;
    uint32_t start_sector;
    uint32_t parent_sector;
    uint64_t size;
    char     resource_tag[8];
    uint32_t name_block;     // 🚀 指向存放名字的块扇区号
    uint16_t name_len;       // 🚀 名字的总字节数
    uint16_t reserved;
};
#pragma pack(pop)
// 定长 40 字节。这样 4096 字节的目录块可以轻松装下 102 个文件/文件夹的名字索引！
inline constexpr uint32_t DIR_ENTRY_HEADER_SIZE = sizeof(DirEntryHeader);
static_assert(DIR_ENTRY_HEADER_SIZE == 40, "DirEntryHeader 必须严格等于 40 字节！");

struct DirEntry {
    char     name[MAX_NAME_LEN + 1];   // 🚀 内存缓冲，存放给上层读取的名字
    uint32_t inode_id;
    uint32_t type;
    uint32_t start_sector;
    uint32_t parent_sector;
    uint64_t size;
    char     resource_tag[8];
    uint32_t reserved;
};

// ==========================================================
// 🚀 物理逻辑图（分区表，支持自定义名字）
// ==========================================================
inline constexpr uint32_t MAX_PARTITIONS = 8;

#pragma pack(push, 1)
struct PartitionEntry {
    char     part_name[32];
    uint32_t start_block;
    uint32_t end_block;
    uint32_t root_inode;
    uint32_t type;
    uint8_t  reserved[32];
};

struct PhysicalLogicalMap {
    uint32_t magic;
    uint32_t part_count;
    PartitionEntry parts[MAX_PARTITIONS];
    uint8_t  reserved[128];
};
#pragma pack(pop)
static_assert(sizeof(PhysicalLogicalMap) <= BLOCK_SIZE, "物理逻辑图大小不能超过一个块！");

// ==========================================================
// 🚀 超级块
// ==========================================================
#pragma pack(push, 1)
struct Superblock {
    uint32_t magic;
    uint32_t total_blocks;
    uint32_t root_dir_start;
    uint32_t data_start;
    uint32_t free_blocks;
    char     volume_label[32];
    uint32_t mounted;

    uint32_t inode_table_start;
    uint32_t inode_table_blocks;
    uint32_t inode_count;

    uint32_t domain_table_start;
    uint32_t domain_table_blocks;
    uint32_t domain_count;

    uint32_t link_table_start;
    uint32_t link_table_blocks;
    uint32_t link_count;

    uint32_t block_size;
    uint32_t reserved_blocks;

    uint32_t checksum;         
};
#pragma pack(pop)
static_assert(sizeof(Superblock) == 104, "Superblock 必须严格等于 104 字节！");

// ==========================================================
// 🚀 【新增 1】作用块 (Scope Block)
// ==========================================================
inline constexpr uint32_t SCOPE_ROOT_ID       = 0;
inline constexpr uint32_t SCOPE_NAME_MAX      = 32;
inline constexpr uint32_t SCOPE_LETTER_COUNT  = 26;
inline constexpr uint32_t SCOPE_DIGIT_COUNT   = 10;
inline constexpr uint32_t SCOPE_CHILD_PER_LTR = SCOPE_DIGIT_COUNT;
inline constexpr uint32_t SCOPE_CHILD_PER_LVL = SCOPE_LETTER_COUNT * SCOPE_DIGIT_COUNT;

#pragma pack(push, 1)
struct ScopeEntry {
    uint32_t scope_id;
    uint32_t parent_id;
    uint32_t level;
    uint32_t inode_id;
    char     name[SCOPE_NAME_MAX];
    uint32_t child_count;
    uint32_t flags;
    uint32_t reserved[3];
};
#pragma pack(pop)
static_assert(sizeof(ScopeEntry) <= BLOCK_SIZE, "ScopeEntry 不能超过一块！");
inline constexpr uint32_t SCOPE_PER_BLOCK = BLOCK_SIZE / sizeof(ScopeEntry);

#pragma pack(push, 1)
struct ScopeTableHeader {
    uint32_t magic;                 
    uint32_t scope_count;
    uint32_t next_table_block;
    uint32_t root_scope_id;
};
#pragma pack(pop)

// ==========================================================
// 🚀 【新增 2】域遍布 (Virtual Bitmap)
// ==========================================================
inline constexpr uint32_t VBITMAP_NODE_SIZE = 16;
inline constexpr uint32_t VBITMAP_PER_BLOCK = BLOCK_SIZE / VBITMAP_NODE_SIZE;              
inline constexpr uint32_t VBITMAP_MAX_NODES = (VBITMAP_TABLE_BLOCKS_DEF - 1) * VBITMAP_PER_BLOCK;  

#pragma pack(push, 1)
struct VirtualBitmapHeader {
    uint32_t magic;                 
    uint32_t node_count;            
    uint32_t next_block;
    uint32_t checksum;
};

struct VirtualBitmapNode {
    uint32_t node_index;
    uint32_t domain_id;
    uint32_t inode_start;
    uint32_t flags;                 
};
#pragma pack(pop)
static_assert(sizeof(VirtualBitmapHeader) == 16, "");
static_assert(sizeof(VirtualBitmapNode)   == 16, "");

// ==========================================================
// 🚀 【新增 3】静态码 (Static Code) —— 严格 196 字节
// ==========================================================
inline constexpr uint32_t STATIC_CODE_SIZE    = 196;
inline constexpr uint32_t STATIC_JUMP_SIZE    = 96;
inline constexpr uint32_t STATIC_BITMAP_SIZE  = 52;
inline constexpr uint32_t STATIC_ADDR_SIZE    = 18;
inline constexpr uint32_t STATIC_HIDDEN_SIZE  = 30;
inline constexpr uint32_t STATIC_BITMAP_BITS  = STATIC_BITMAP_SIZE * 8;   

#pragma pack(push, 1)
union StaticJump {
    uint8_t     raw[STATIC_JUMP_SIZE];
    BlockHeader header;
};

struct StaticAddress {
    uint8_t part_addr[6];
    uint8_t domain_addr[6];
    uint8_t link_addr[6];
};

struct StaticCode {
    StaticJump    jump;                          
    uint8_t       bitmap[STATIC_BITMAP_SIZE];    
    StaticAddress addr;                          
    uint8_t       hidden[STATIC_HIDDEN_SIZE];    
};
#pragma pack(pop)
static_assert(sizeof(StaticJump)    == STATIC_JUMP_SIZE,   "StaticJump 必须 96 字节！");
static_assert(sizeof(StaticAddress) == STATIC_ADDR_SIZE,   "StaticAddress 必须 18 字节！");
static_assert(sizeof(StaticCode)    == STATIC_CODE_SIZE,   "StaticCode 必须严格等于 196 字节！");

// ==========================================================
// 🚀 【新增 4】动态头 (Dynamic Header)
// ==========================================================
inline constexpr uint32_t DYN_HEAD_PREFIX_SIZE = 16;

inline constexpr uint16_t DYN_SEC_NAME     = 0x0001;
inline constexpr uint16_t DYN_SEC_INFO     = 0x0002;
inline constexpr uint16_t DYN_SEC_FILE_HDR = 0x0003;
inline constexpr uint16_t DYN_SEC_DIR_HDR  = 0x0004;
inline constexpr uint16_t DYN_SEC_TAG      = 0x0005;
inline constexpr uint16_t DYN_SEC_PATH     = 0x0006;
inline constexpr uint16_t DYN_SEC_EXT      = 0x7FFF;

#pragma pack(push, 1)
struct DynamicHeaderPrefix {
    uint32_t magic;                 
    uint32_t type;                  
    uint32_t total_size;
    uint32_t section_count;
};

struct DynamicSectionHeader {
    uint16_t sec_type;              
    uint16_t sec_len;
};
#pragma pack(pop)
static_assert(sizeof(DynamicHeaderPrefix)  == DYN_HEAD_PREFIX_SIZE, "");
static_assert(sizeof(DynamicSectionHeader) == 4, "");

// ==========================================================
// 🚀 【新增 5】物理逻辑头 (Physical Logical Header)
// ==========================================================
inline constexpr uint32_t PLH_TAG_FIRST = 0;
inline constexpr uint32_t PLH_TAG_OTHER = 1;

#pragma pack(push, 1)
struct PhysicalLogicalHeader {
    uint32_t magic;                 
    uint32_t part_index;
    uint32_t start_block;
    uint32_t end_block;
    uint32_t inode_table_start;
    uint32_t inode_table_blocks;
    uint32_t flags;                 
    char     tag[6];                
    uint32_t reserved[10];
};
#pragma pack(pop)
static_assert(sizeof(PhysicalLogicalHeader) <= BLOCK_SIZE, "");

// ==========================================================
// DiskFileSystem
// ==========================================================
class DiskFileSystem {
public:
    static void format(BlockDevice* disk, const std::string& label, bool create_defaults = true);
    
    // 🚀 核心修复：加上默认参数，完美兼容之前的旧调用
    static void rebuild_bitmap(BlockDevice* disk, uint32_t part_start = 0, uint32_t part_end = 0);

    static bool find_entry(BlockDevice* disk, const std::string& vfs_path, DirEntry& out_entry);
    static bool create_file(BlockDevice* disk, const std::string& vfs_path);
    static bool create_directory(BlockDevice* disk, const std::string& vfs_path);
    static bool write_file(BlockDevice* disk, const std::string& vfs_path, const std::string& content);
    static std::string read_file_content(BlockDevice* disk, const std::string& vfs_path);
    static bool delete_entry(BlockDevice* disk, const std::string& vfs_path);
    static std::vector<std::string> list_dir(BlockDevice* disk, const std::string& vfs_path);

    static bool move_entry(BlockDevice* disk, const std::string& src, const std::string& dst);
    static bool copy_entry(BlockDevice* disk, const std::string& src, const std::string& dst);
    static bool rename_entry(BlockDevice* disk, const std::string& old_path, const std::string& new_path);

    static bool get_mounted_flag(BlockDevice* disk);
    static void set_mounted_flag(BlockDevice* disk, bool mounted);

    static std::string get_resource_tag(BlockDevice* disk, const std::string& vfs_path);
    static bool        set_resource_tag(BlockDevice* disk, const std::string& vfs_path, const std::string& tag);

    // 🚀 物理逻辑图
    static bool read_partition_table(BlockDevice* disk, PhysicalLogicalMap& out_map);
    static bool write_partition_table(BlockDevice* disk, const PhysicalLogicalMap& in_map);

    // 🚀 块分配 / 释放
    static uint32_t allocate_sector(BlockDevice* disk);
    static void     free_sector(BlockDevice* disk, uint32_t sector);

    static uint32_t inode_alloc(BlockDevice* disk, uint32_t type,
                                 uint32_t parent_inode, uint32_t domain_id = 0);

    static bool     inode_read(BlockDevice* disk, uint32_t inode_id, InodeEntry& out);
    static bool     inode_write(BlockDevice* disk, uint32_t inode_id, const InodeEntry& in);
    static bool     inode_free(BlockDevice* disk, uint32_t inode_id);

    static bool     index_read(BlockDevice* disk, uint32_t index_block, IndexBlock& out);
    static bool     index_write(BlockDevice* disk, uint32_t index_block, const IndexBlock& in);
    static uint32_t index_alloc(BlockDevice* disk);
    static void     index_free(BlockDevice* disk, uint32_t index_block);
    static bool     index_free_with_data(BlockDevice* disk, uint32_t index_block); 

    // 🚀 域表
    static bool     domain_read(BlockDevice* disk, uint32_t domain_id, DomainEntry& out);
    static bool     domain_write(BlockDevice* disk, uint32_t domain_id, const DomainEntry& in);
    static uint32_t domain_alloc(BlockDevice* disk, uint32_t parent_id,
                                  const std::string& name);
    static bool     domain_free(BlockDevice* disk, uint32_t domain_id);
    static int32_t  domain_find_by_name(BlockDevice* disk, const std::string& name);

    // 🚀 链接表
    static bool     link_read(BlockDevice* disk, uint32_t link_id, LinkEntry& out);
    static bool     link_write(BlockDevice* disk, uint32_t link_id, const LinkEntry& in);
    static uint32_t link_alloc(BlockDevice* disk, uint32_t parent_inode,
                                uint32_t child_inode, uint32_t domain_id,
                                const std::string& name);
    static bool     link_free(BlockDevice* disk, uint32_t link_id);
    static bool     link_free_by_nodes(BlockDevice* disk, uint32_t parent_inode, uint32_t child_inode);

    static bool journal_begin(BlockDevice* disk, const std::string& path,
                              const std::string& data, uint32_t target_block);
    static bool journal_commit(BlockDevice* disk);
    static bool journal_abort(BlockDevice* disk);
    static bool journal_has_pending(BlockDevice* disk);
    static std::string journal_recover(BlockDevice* disk);

    static bool verify_write_backup(BlockDevice* disk);
    static bool verify_check(BlockDevice* disk);

    static bool hidden_write_backup(BlockDevice* disk);
    static std::string hidden_read_backup(BlockDevice* disk);

    static std::string get_filename(const std::string& vfs_path);
    static std::string get_parent_path(const std::string& vfs_path);

    // 🚀 失败返回 INVALID_SECTOR
    static uint32_t find_dir_sector(BlockDevice* disk, const std::string& vfs_path);

    static bool is_name_conflict(BlockDevice* disk, uint32_t dir_sector, const std::string& name);

    static bool inode_bitmap_test(BlockDevice* disk, uint32_t inode_id);
    static void inode_bitmap_set(BlockDevice* disk, uint32_t inode_id, bool used);

    static uint32_t simple_checksum(const uint8_t* data, size_t len);

    // ======================================================
    // 🚀 作用块 (Scope Block)
    // ======================================================
    static std::string scope_make_name(uint32_t level, uint32_t child_index);
    static bool        scope_read(BlockDevice* disk, uint32_t scope_id, ScopeEntry& out);
    static bool        scope_write(BlockDevice* disk, uint32_t scope_id, const ScopeEntry& in);
    static uint32_t    scope_alloc(BlockDevice* disk, uint32_t parent_id, uint32_t level);
    static bool        scope_free(BlockDevice* disk, uint32_t scope_id);
    static bool        scope_enter(BlockDevice* disk, uint32_t parent_scope_id, uint32_t& out_child_id);
    static bool        scope_back(BlockDevice* disk, uint32_t scope_id, uint32_t& out_parent_id);
    static std::string scope_path_string(BlockDevice* disk, uint32_t scope_id);

    // ======================================================
    // 🚀 域遍布 (Virtual Bitmap)
    // ======================================================
    static bool vbitmap_append(BlockDevice* disk, uint32_t domain_id);
    static bool vbitmap_remove(BlockDevice* disk, uint32_t domain_id);
    static bool vbitmap_read(BlockDevice* disk, VirtualBitmapHeader& hdr,
                             std::vector<VirtualBitmapNode>& out_nodes);
    static bool vbitmap_rebuild(BlockDevice* disk);

    // ======================================================
    // 🚀 查树图 (Physical vs Virtual)
    // ======================================================
    static bool tree_map_check(BlockDevice* disk);
    static bool tree_map_rebuild_virtual(BlockDevice* disk);
    static bool tree_map_ensure(BlockDevice* disk);

    // ======================================================
    // 🚀 静态码
    // ======================================================
    static bool static_code_read (BlockDevice* disk, uint32_t block, StaticCode& out);
    static bool static_code_write(BlockDevice* disk, uint32_t block, const StaticCode& in);
    static bool static_bit_test  (const StaticCode& code, uint32_t bit);
    static void static_bit_set   (StaticCode& code, uint32_t bit, bool used);
    static bool static_addr_pack (StaticCode& code, uint32_t part_block,
                                  uint32_t domain_block, uint32_t link_block);

    // ======================================================
    // 🚀 动态头
    // ======================================================
    static bool dynamic_header_write(BlockDevice* disk, uint32_t block, uint32_t type,
                                     const std::vector<std::pair<uint16_t, std::string>>& sections);
    static bool dynamic_header_read (BlockDevice* disk, uint32_t block, uint32_t& out_type,
                                     std::vector<std::pair<uint16_t, std::string>>& out_sections);
    static bool dynamic_header_find (BlockDevice* disk, uint32_t block,
                                     uint16_t sec_type, std::string& out_payload);
    static bool dynamic_header_append(BlockDevice* disk, uint32_t block,
                                      uint16_t sec_type, const std::string& payload);

    // ======================================================
    // 🚀 物理逻辑头
    // ======================================================
    static bool plh_exists (BlockDevice* disk);
    static bool plh_generate(BlockDevice* disk);
    static bool plh_read   (BlockDevice* disk, uint32_t part_index, PhysicalLogicalHeader& out);
    static bool plh_write  (BlockDevice* disk, uint32_t part_index, const PhysicalLogicalHeader& in);
    static bool plh_sync_from_partition(BlockDevice* disk);
};