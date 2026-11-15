#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <cstdint>
#include <functional>
#include <shared_mutex> // 🚀 线程同步
#include <mutex>

// 前向声明你的底层驱动和文件系统，避免头文件循环包含
class DiskImageDriver;
class DiskFileSystem;
class BlockDevice;

// ================= 权限系统定义 =================
enum class UserRole : int {
    R0_ULTIMATE = 0, R1_SUPER_ADMIN = 1, R2_HW_ADMIN = 2,
    R3_SYS_ADMIN = 3, R4_GENERAL_ADMIN = 4, R5_MEMBER = 5, R6_GUEST = 6
};

// ================= 资源类型定义 =================
enum class ResourceType {
    PUBLIC,    // 公共资源：无权限限制
    PRIVATE,   // 私有资源：家目录里面的东西
    SPECIAL,   // 特殊资源：通过指令创建的，如 .git
    GROUP,     // 组资源：只有该组成员能访问和使用
    SYSTEM     // 系统资源：暂不开放操作
};

// ================= 🚀 资源权限位 =================
enum ResourcePerm : uint32_t {
    PERM_NONE  = 0,
    PERM_READ  = 1u << 0,   // r 读（查看内容）
    PERM_WRITE = 1u << 1,   // w 写（追加/覆盖）
    PERM_MOD   = 1u << 2,   // a 改（重命名/移动）
    PERM_DEL   = 1u << 3,   // m 删（删除自己）
    PERM_CLEAR = 1u << 4,   // n 清（清空内容）
    PERM_LIST  = 1u << 5,   // k 查（列目录内容，仅目录）
    PERM_EXEC  = 1u << 6,   // e 执（执行权限）
    PERM_ALL   = 0x7F       // 🚀 全权限（7 位全开）
};

// ============================================================
// 🚀 资源元数据
// ============================================================
struct ResourceInfo {
    ResourceType type = ResourceType::PUBLIC;
    int owner_id = -1;
    int group_id = -1;
    uint32_t perms = PERM_ALL;
    uint32_t domain_id = 0;
};

// ============================================================
// 🚀 节点基类
// ============================================================
class Node : public std::enable_shared_from_this<Node> {
public:
    std::string name;
    std::weak_ptr<Node> parent; // 🚀 优化：改用 weak_ptr，防止内存循环引用导致泄漏
    uint32_t inode_id  = 0;     
    uint32_t domain_id = 0;     
    uint64_t size      = 0;     
    bool     is_dir    = false; // 🚀 增加类型标记

    Node(std::string n, std::shared_ptr<Node> p = nullptr) 
        : name(std::move(n)), parent(p) {}
    virtual ~Node() = default;
};

// 文件节点
class File : public Node {
public:
    // 🚀 注意：content 仅作为小文件（如配置文件）的内存缓存。
    // 大文件千万不要全量读入 content，应直接走 DiskFileSystem 的流式读写，否则会 OOM
    std::string content;
    
    File(std::string n, std::string c = "", std::shared_ptr<Node> p = nullptr) 
        : Node(std::move(n), p), content(std::move(c)) {
        is_dir = false;
        size = content.size();
    }
};

// 目录节点
class Directory : public Node {
public:
    std::map<std::string, std::shared_ptr<Node>> children;
    uint32_t start_sector = 0; 
    
    Directory(std::string n, uint32_t sector = 0, std::shared_ptr<Node> p = nullptr) 
        : Node(std::move(n), p), start_sector(sector) {
        is_dir = true;
    }
    void add_child(std::shared_ptr<Node> node);
};

// ============================================================
// 🚀 核心 VFS 系统（单例）
// ============================================================
class VFS {
public:
    static VFS& getInstance();
    
    void init(const std::string& volume_label = "/");
    
    // ================= 路径与节点 =================
    void set_cwd(const std::string& new_cwd);
    std::string get_cwd() const;
    std::string get_display_path(const std::string& vfs_path) const;
    std::string normalize_path(const std::string& input) const;
    
    // 🚀 获取节点（如果是大目录，应尽量避免全量递归 get_node）
    std::shared_ptr<Node> get_node(const std::string& vfs_path);

    // ================= 文件操作 =================
    std::string create_file(const std::string& vfs_path, const std::string& content = "");
    std::string create_directory(const std::string& vfs_path);
    
    // 🚀 为了不 OOM，读大文件时，建议外部调用 DiskFileSystem 分块读，而不是直接 read_file
    std::string read_file(const std::string& vfs_path);
    
    std::string write_file(const std::string& vfs_path, const std::string& content);
    std::string append_file(const std::string& vfs_path, const std::string& content);
    
    std::string delete_node(const std::string& vfs_path);
    std::vector<std::string> list_directory(const std::string& vfs_path);
    std::string move_node(const std::string& src_path, const std::string& dst_path);
    std::string copy_node(const std::string& src_path, const std::string& dst_path);
    std::string rename_node(const std::string& old_path, const std::string& new_path);

    uint64_t get_dir_size(const std::string& vfs_path);

    // ================= 权限系统接口 =================
    void set_user_id(int id);                              
    int get_user_id() const;    

    UserRole get_role() const;     
    void set_role(UserRole r);        

    void set_sudo_origin(int uid);
    void clear_sudo_origin();
    int  get_effective_owner() const;

    std::string get_role_name() const;                     
    bool check_permission(UserRole required_role) const;    
    bool check_cmd_permission(const std::string& cmd_name) const; 

    // ================= 资源系统接口 =================
    ResourceType get_resource_type(const std::string& vfs_path) const;
    bool check_resource_access(const std::string& vfs_path) const;
    void register_special_resource(const std::string& vfs_path, ResourceType type, int group_id = -1);

    ResourceInfo get_resource_info(const std::string& vfs_path) const;
    void         set_resource_info(const std::string& vfs_path, const ResourceInfo& info);

    ResourceInfo get_resource_info_dynamic(const std::string& vfs_path) const;
    uint32_t get_default_perms(ResourceType type, const std::string& vfs_path) const;

    bool check_perm(const std::string& vfs_path, uint32_t perm) const;
    void set_perm(const std::string& vfs_path, uint32_t perm);

    // ============================================================
    // 🚀 受保护路径
    // ============================================================
    void add_protected_path(const std::string& vfs_path);
    void remove_protected_path(const std::string& vfs_path);
    bool is_protected_path(const std::string& vfs_path) const;

    // ============================================================
    // 🚀 挂载系统接口
    // ============================================================
    struct MountEntry {
        std::shared_ptr<DiskImageDriver> driver;
        std::function<std::shared_ptr<Node>(const std::string&)> resolver;
    };

    bool mount_disk(const std::string& mount_point,
                    std::shared_ptr<DiskImageDriver> driver,
                    std::function<std::shared_ptr<Node>(const std::string&)> resolver);
    bool unmount_disk(const std::string& mount_point);

    bool find_mount(const std::string& vfs_path,
                    std::string& out_mount_point,
                    MountEntry& out_entry);

    BlockDevice* get_disk_driver() const;

private:
    VFS() = default;
    ~VFS() = default; // 🚀 单例需要私有析构
    VFS(const VFS&) = delete;
    VFS& operator=(const VFS&) = delete;

    mutable std::shared_mutex vfs_mutex; // 🚀 全局读写锁，保护 VFS 内的所有数据结构

    std::shared_ptr<Directory> invisible_root;
    std::shared_ptr<Directory> trunk;
    
    std::string current_cwd = "/"; 
    std::shared_ptr<DiskImageDriver> disk_driver;
    
    int current_user_id = 0;
    UserRole current_role = UserRole::R3_SYS_ADMIN;

    std::map<std::string, ResourceInfo> resource_registry; 
    int current_group_id = -1; 
    int sudo_origin_uid  = -9999;

    std::map<std::string, MountEntry> mount_table;
    std::set<std::string> protected_paths;

    // ================= 内部辅助函数 =================
    bool is_home_directory(const std::string& vfs_path) const;
    static bool path_starts_with(const std::string& path, const std::string& prefix);
    
    // 🚀 核心新增：去底层分区表查询盘符是否存在（用于路径解析）
    bool has_drive_letter(char drive) const; 
};