#include "dcl_drt.h"
#include "disk_image_driver.h"
#include "disk_fs.h"
#include "vfs.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cctype>
#include <algorithm>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <iomanip>

namespace fs = std::filesystem;

// ============================================================
// 🚀 全局虚拟硬盘注册表
// ============================================================
std::map<std::string, VirtualDiskInfo> g_virtual_disks;

// ============================================================
// 🚀 账户表
// ============================================================
struct UserAccount {
    std::string name;
    std::string login_password;
    std::string verify_password;
    std::string phone;
    std::string email;
    UserRole    role;
    std::string source;
    std::string state;
    int         uid;
};
std::map<std::string, UserAccount> g_users;
std::string g_current_user = "XY";

static const std::string COLON = "：";

static bool is_admin_level() {
    return static_cast<int>(VFS::getInstance().get_role())
        <= static_cast<int>(UserRole::R3_SYS_ADMIN);
}

static std::string role_to_str(UserRole r) {
    switch (r) {
        case UserRole::R0_ULTIMATE:      return "R0";
        case UserRole::R1_SUPER_ADMIN:   return "R1";
        case UserRole::R2_HW_ADMIN:      return "R2";
        case UserRole::R3_SYS_ADMIN:     return "R3";
        case UserRole::R4_GENERAL_ADMIN: return "R4";
        case UserRole::R5_MEMBER:        return "R5";
        case UserRole::R6_GUEST:         return "R6";
    }
    return "?";
}

static bool str_to_role(const std::string& s, UserRole& out) {
    if (s == "R0") { out = UserRole::R0_ULTIMATE;      return true; }
    if (s == "R1") { out = UserRole::R1_SUPER_ADMIN;   return true; }
    if (s == "R2") { out = UserRole::R2_HW_ADMIN;      return true; }
    if (s == "R3") { out = UserRole::R3_SYS_ADMIN;     return true; }
    if (s == "R4") { out = UserRole::R4_GENERAL_ADMIN; return true; }
    if (s == "R5") { out = UserRole::R5_MEMBER;        return true; }
    if (s == "R6") { out = UserRole::R6_GUEST;         return true; }
    return false;
}

static int next_uid_for_role(UserRole role) {
    int base = 1, end = 100;
    switch (role) {
        case UserRole::R0_ULTIMATE:      return -3;
        case UserRole::R1_SUPER_ADMIN:   return -2;
        case UserRole::R2_HW_ADMIN:      return -1;
        case UserRole::R3_SYS_ADMIN:     return 0;
        case UserRole::R4_GENERAL_ADMIN: base = 1;   end = 100;  break;
        case UserRole::R5_MEMBER:        base = 101; end = 300;  break;
        default:                         base = 301; end = 9999; break;
    }
    for (int uid = base; uid <= end; ++uid) {
        bool used = false;
        for (auto& [n, u] : g_users) if (u.uid == uid) { used = true; break; }
        if (!used) return uid;
    }
    return -1;
}

static std::string strip_color(const std::string& s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
            size_t j = i + 2;
            while (j < s.size() && s[j] != 'm') j++;
            if (j < s.size()) j++;
            i = j;
        } else {
            out += s[i++];
        }
    }
    return out;
}

static std::string user_config_path(const UserAccount& u) {
    if (u.role == UserRole::R3_SYS_ADMIN) return "/usr/R3.xyi";
    return "/usr/" + u.name + ".xyi";
}

static std::string role_to_perm_type(UserRole r) {
    if (r == UserRole::R3_SYS_ADMIN)     return "xyby";
    if (r == UserRole::R4_GENERAL_ADMIN) return "sudo";
    return "";
}

static void ensure_home_dir(const std::string& name, int uid) {
    if (!VFS::getInstance().get_node("/home")) VFS::getInstance().create_directory("/home");
    if (!VFS::getInstance().get_node("/ast")) VFS::getInstance().create_directory("/ast");

    std::string hp = "/home/" + name;
    if (!VFS::getInstance().get_node(hp)) VFS::getInstance().create_directory(hp);
    ResourceInfo ri_home;
    ri_home.type = ResourceType::PRIVATE; ri_home.owner_id = uid; ri_home.group_id = -1; ri_home.perms = PERM_ALL;
    VFS::getInstance().set_resource_info(hp, ri_home);

    std::vector<std::string> std_dirs = { "音乐", "文档", "文本", "图片", "视频", "下载" };
    for (const auto& dir_name : std_dirs) {
        std::string dir_path = "/home/" + name + "/" + dir_name;
        if (!VFS::getInstance().get_node(dir_path)) VFS::getInstance().create_directory(dir_path);
        ResourceInfo ri_dir;
        ri_dir.type = ResourceType::PRIVATE; ri_dir.owner_id = uid; ri_dir.group_id = -1; ri_dir.perms = PERM_ALL;
        VFS::getInstance().set_resource_info(dir_path, ri_dir);
    }

    std::string ap = "/ast/" + name;
    if (!VFS::getInstance().get_node(ap)) VFS::getInstance().create_directory(ap);
    ResourceInfo ri_ast;
    ri_ast.type = ResourceType::PRIVATE; ri_ast.owner_id = uid; ri_ast.group_id = -1; ri_ast.perms = PERM_ALL;
    VFS::getInstance().set_resource_info(ap, ri_ast);
}

static void save_user_to_vfs(const UserAccount& u) {
    std::string path = user_config_path(u);
    bool was_protected = VFS::getInstance().is_protected_path(path);
    VFS::getInstance().remove_protected_path(path);

    std::string data;
    data += "账户名：" + u.name + "\n";
    data += "登录密码：" + u.login_password + "\n";
    data += "验证密码：" + u.verify_password + "\n";
    data += "手机号：" + u.phone + "\n";
    data += "电子邮箱：" + u.email + "\n";
    data += "权限：" + role_to_perm_type(u.role) + "\n";
    data += "权限来源：" + u.source + "\n";
    data += "权限状态：" + role_to_str(u.role) + "\n";
    data += "ID:" + std::to_string(u.uid) + "\n";

    VFS::getInstance().delete_node(path);
    VFS::getInstance().create_file(path, data);

    VFS::getInstance().add_protected_path(path);
    VFS::getInstance().set_perm(path, PERM_READ);
    (void)was_protected;
}

static void delete_user_from_vfs(const UserAccount& u) {
    std::string path = user_config_path(u);
    VFS::getInstance().remove_protected_path(path);
    VFS::getInstance().delete_node(path);
}

static bool load_user_from_vfs(const std::string& path) {
    std::string content = VFS::getInstance().read_file(path);
    if (content.empty()) return false;

    UserAccount u;
    u.role = UserRole::R6_GUEST; u.source = "星缘编语"; u.state = "正常"; u.uid = -1;

    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t pos = line.find(COLON);
        if (pos != std::string::npos) {
            std::string k = line.substr(0, pos);
            std::string v = line.substr(pos + COLON.size());
            if      (k == "账户名")   u.name = v;
            else if (k == "登录密码") u.login_password = v;
            else if (k == "验证密码") u.verify_password = v;
            else if (k == "手机号")   u.phone = v;
            else if (k == "电子邮箱") u.email = v;
            else if (k == "权限来源") u.source = v;
            else if (k == "权限状态") { UserRole r; if (str_to_role(v, r)) u.role = r; }
            continue;
        }
        pos = line.find(':');
        if (pos != std::string::npos) {
            std::string k = line.substr(0, pos);
            std::string v = line.substr(pos + 1);
            if (k == "ID") { try { u.uid = std::stoi(v); } catch (...) {} }
        }
    }
    if (u.name.empty()) return false;
    g_users[u.name] = u;
    VFS::getInstance().add_protected_path(path);
    VFS::getInstance().set_perm(path, PERM_READ);
    return true;
}

void user_init() {
    if (!VFS::getInstance().get_node("/usr")) VFS::getInstance().create_directory("/usr");

    std::vector<std::string> items = VFS::getInstance().list_directory("/usr");
    for (auto& raw : items) {
        std::string name = strip_color(raw);
        if (name.size() < 4 || name.substr(name.size() - 4) != ".xyi") continue;
        std::string path = "/usr/" + name;
        auto node = VFS::getInstance().get_node(path);
        if (!node || dynamic_cast<Directory*>(node.get())) continue;
        load_user_from_vfs(path);
        VFS::getInstance().add_protected_path(path);
        VFS::getInstance().set_perm(path, PERM_READ);
    }

    bool has_r3 = false;
    for (auto& [n, u] : g_users) if (u.role == UserRole::R3_SYS_ADMIN) { has_r3 = true; break; }

    if (!has_r3) {
        UserAccount u;
        u.name = "XY"; u.login_password = ""; u.verify_password = "xingyuan";
        u.phone = ""; u.email = ""; u.role = UserRole::R3_SYS_ADMIN;
        u.source = "星缘编语"; u.state = "正常"; u.uid = 0;
        g_users["XY"] = u;
        save_user_to_vfs(u);
    }

    for (auto& [n, u] : g_users) ensure_home_dir(n, u.uid);

    if (g_users.count("XY")) g_current_user = "XY";
    else if (!g_users.empty()) g_current_user = g_users.begin()->first;

    auto& cur = g_users[g_current_user];
    VFS::getInstance().set_user_id(cur.uid);
    VFS::getInstance().set_role(cur.role);
}

bool drt_verify_r3_password(const std::string& input) {
    for (auto& [name, u] : g_users) if (u.role == UserRole::R3_SYS_ADMIN) return u.verify_password == input;
    return false;
}

std::string drt_get_r3_name() {
    for (auto& [name, u] : g_users) if (u.role == UserRole::R3_SYS_ADMIN) return name;
    return "R3";
}

int drt_get_uid_by_name(const std::string& name) {
    auto it = g_users.find(name);
    return (it != g_users.end()) ? it->second.uid : -9999;
}

std::string drt_get_name_by_uid(int uid) {
    for (auto& [name, u] : g_users) if (u.uid == uid) return name;
    return "";
}

static std::string user_cmd(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::string out = "========== 账户列表 ==========\n";
        for (auto& [name, u] : g_users) {
            out += "  " + name + "  (" + role_to_str(u.role) + ", uid=" + std::to_string(u.uid) + ", 来源=" + u.source + ", 状态=" + u.state + ")";
            if (name == g_current_user) out += "  ← 当前";
            out += "\n";
        }
        out += "==============================\n";
        return out;
    }
    if (args[0] == "whoami") {
        auto& u = g_users[g_current_user];
        return "当前账户: " + u.name + "  (" + role_to_str(u.role) + ", uid=" + std::to_string(u.uid) + ")";
    }
    if (args[0] == "add") {
        if (!is_admin_level()) return "错误: 权限不足，创建账户需要 R0~R3 权限";
        if (args.size() < 2) return "用法: user add <名字> [登录密码]";
        std::string name = args[1]; std::string pass = (args.size() >= 3) ? args[2] : "";
        if (g_users.count(name)) return "错误: 账户已存在: " + name;
        UserRole role = UserRole::R5_MEMBER; int uid = next_uid_for_role(role);
        UserAccount u;
        u.name = name; u.login_password = pass; u.verify_password = ""; u.phone = ""; u.email = "";
        u.role = role; u.source = "星缘编语"; u.state = "正常"; u.uid = uid;
        g_users[name] = u; save_user_to_vfs(u); ensure_home_dir(name, uid);
        return "[+] 已创建账户: " + name + "  (R5, uid=" + std::to_string(uid) + ")";
    }
    if (args[0] == "switch" || args[0] == "su") {
        if (args.size() < 2) return "用法: user switch <名字> [登录密码]";
        auto it = g_users.find(args[1]);
        if (it == g_users.end()) return "错误: 账户不存在: " + args[1];
        if (it->second.state != "正常") return "错误: 账户 '" + args[1] + "' 已被禁用";
        if (!it->second.login_password.empty() && (args.size() < 3 || args[2] != it->second.login_password)) return "错误: 登录密码不正确";
        g_current_user = args[1];
        VFS::getInstance().set_user_id(it->second.uid);
        VFS::getInstance().set_role(it->second.role);
        return "[+] 已切换到: " + args[1];
    }
    return "未知子命令";
}

static std::string normalize_disk_name(const std::string& raw) {
    std::string name = raw;
    if (name.size() > 4) {
        std::string tail = name.substr(name.size() - 4);
        for (auto& c : tail) c = (char)std::tolower((unsigned char)c);
        if (tail == ".img") name = name.substr(0, name.size() - 4);
    }
    return name;
}

static std::string drt_usage() {
    return
        "========== drt 磁盘管理 ==========\n"
        "  drt disk /                               查询主盘容量及分区\n"
        "  drt disk <盘名/分区名>                   查询指定盘或分区的容量\n"
        "  drt disk <盘名> <容量MB>                 创建全新虚拟硬盘\n"
        "  drt disk / <新分区名> <容量MB>           从主盘切出新分区\n"
        "  drt disk <盘名> <新分区名> <容量MB>      从指定虚拟盘切出新分区\n"
        "  drt disk <分区名> del                    删除分区，容量收回未分配区\n"
        "  drt disk <分区名> /                      将剩余未分配空间全给该分区\n"
        "  drt disk <旧名> / <新名>                 重命名分区\n"
        "  drt <盘名> -T                            挂载虚拟硬盘\n"
        "  drt <盘名> -F                            卸载虚拟硬盘\n"
        "  drt img                                  列出所有已挂载的虚拟硬盘\n"
        "======================================\n";
}

// ============================================================
// 🚀 核心助手：获取盘的操作句柄
// ============================================================
struct DiskHandle {
    BlockDevice* driver = nullptr;
    std::shared_ptr<DiskImageDriver> virtual_driver = nullptr; 
};

static DiskHandle get_disk_handle(const std::string& name) {
    DiskHandle h;
    if (name == "/" || name == "disk") {
        h.driver = VFS::getInstance().get_disk_driver();
    } else {
        std::string norm = normalize_disk_name(name);
        auto it = g_virtual_disks.find(norm);
        if (it != g_virtual_disks.end()) {
            h.virtual_driver = std::make_shared<DiskImageDriver>();
            if (h.virtual_driver->open(it->second.path)) {
                h.driver = h.virtual_driver.get();
            }
        }
    }
    return h;
}

// ============================================================
// 🚀 1. 创建虚拟硬盘 (仅针对非主盘)
// ============================================================
static std::string drt_create(const std::string& raw_name, const std::string& size_str) {
    if (!is_admin_level()) return "[-] 错误: 权限不足，创建虚拟硬盘需要 R0~R3 权限";

    std::string name = normalize_disk_name(raw_name);
    if (name.empty()) return "[-] 错误: 缺少硬盘名字。";
    if (g_virtual_disks.count(name)) return "[-] 错误: 硬盘 '" + name + "' 已存在。";

    uint32_t size_mb = 0;
    try { size_mb = (uint32_t)std::stoul(size_str); } catch (...) {
        return "[-] 错误: 容量必须是整数（MB）。";
    }
    if (size_mb < 1) return "[-] 错误: 容量至少 1MB。";

    std::string disk_path = name + ".img";
    if (fs::exists(disk_path)) return "[-] 错误: 物理文件 '" + disk_path + "' 已存在。";

    auto driver = std::make_shared<DiskImageDriver>();
    driver->create_image(disk_path, size_mb);
    if (!fs::exists(disk_path)) return "[-] 错误: 创建镜像文件失败。";

    DiskFileSystem::format(driver.get(), "A", false); // 底层不写分区表
    driver->close();

    VirtualDiskInfo info;
    info.name = name; info.path = disk_path; info.size_mb = size_mb; info.mounted = false;
    g_virtual_disks[name] = info;

    return "[+] 虚拟硬盘 '" + name + "' 已创建，容量 " + std::to_string(size_mb) + " MB";
}

// ============================================================
// 🚀 2. 查询容量（支持主盘和虚拟盘）
// ============================================================
static std::string drt_disk_info(const std::string& target = "") {
    std::string out = "========== 磁盘及分区信息 ==========\n";
    bool found = false;

    // 1. 处理主盘查询
    if (target.empty() || target == "/" || target == "disk") {
        found = true;
        BlockDevice* main_driver = VFS::getInstance().get_disk_driver();
        if (main_driver) {
            uint32_t size_mb = main_driver->get_total_sectors() * BLOCK_SIZE / (1024 * 1024);
            out += "硬盘: disk.img (总容量: " + std::to_string(size_mb) + " MB)\n";
            PhysicalLogicalMap pmap{};
            if (DiskFileSystem::read_partition_table(main_driver, pmap) && pmap.part_count > 0) {
                uint32_t total_used_blocks = 0;
                for (uint32_t i = 0; i < pmap.part_count; ++i) {
                    uint32_t blocks = pmap.parts[i].end_block - pmap.parts[i].start_block + 1;
                    uint32_t mb = (blocks * BLOCK_SIZE) / (1024 * 1024);
                    total_used_blocks += blocks;
                    std::string display_name = pmap.parts[i].part_name;
                    if (display_name == "/" || display_name == "[/]") display_name = "未分配/旧盘";
                    out += "  [" + display_name + "] 起: " + 
                           std::to_string(pmap.parts[i].start_block) + " 止: " + 
                           std::to_string(pmap.parts[i].end_block) + " (" + std::to_string(mb) + " MB)\n";
                }
                uint32_t total_allocated_mb = (total_used_blocks * BLOCK_SIZE) / (1024 * 1024);
                out += "  已分配: " + std::to_string(total_allocated_mb) + " MB\n";
                out += "  未分配: " + std::to_string(size_mb - total_allocated_mb) + " MB\n";
            } else {
                out += "  (无分区表，整盘可用)\n";
                out += "  未分配: " + std::to_string(size_mb) + " MB\n";
            }
            out += "--------------------------------------\n";
        }
        if (target == "/" || target == "disk") return out;
    }

    // 2. 处理虚拟盘查询
    for (auto& [name, info] : g_virtual_disks) {
        bool match_disk = (target.empty() || target == name);
        bool disk_has_target = false;
        
        auto driver = std::make_shared<DiskImageDriver>();
        if (!driver->open(info.path)) {
            if (match_disk) out += "硬盘: " + name + " (无法打开镜像)\n";
            continue;
        }

        PhysicalLogicalMap pmap{};
        bool has_part_table = DiskFileSystem::read_partition_table(driver.get(), pmap) && pmap.part_count > 0;

        if (!has_part_table) {
            if (match_disk) {
                found = true;
                out += "硬盘: " + name + " (总容量: " + std::to_string(info.size_mb) + " MB)\n";
                out += "  (无分区表，整盘可用)\n";
                out += "  未分配: " + std::to_string(info.size_mb) + " MB\n";
                out += "--------------------------------------\n";
            }
            driver->close();
            continue;
        }

        for (uint32_t i = 0; i < pmap.part_count; ++i) {
            if (std::string(pmap.parts[i].part_name) == target) {
                disk_has_target = true;
                break;
            }
        }

        if (match_disk || disk_has_target) {
            found = true;
            out += "硬盘: " + name + " (总容量: " + std::to_string(info.size_mb) + " MB)\n";
            
            uint32_t total_used_blocks = 0;
            for (uint32_t i = 0; i < pmap.part_count; ++i) {
                std::string pname = pmap.parts[i].part_name;
                if (!target.empty() && target != name && target != pname) continue;

                uint32_t blocks = pmap.parts[i].end_block - pmap.parts[i].start_block + 1;
                uint32_t mb = (blocks * BLOCK_SIZE) / (1024 * 1024);
                total_used_blocks += blocks;
                
                std::string display_name = pname;
                if (display_name == "/" || display_name == "[/]") display_name = "未分配/旧盘";

                out += "  [" + display_name + "] 起: " + std::to_string(pmap.parts[i].start_block) + 
                       " 止: " + std::to_string(pmap.parts[i].end_block) + 
                       " (" + std::to_string(mb) + " MB)\n";
            }
            uint32_t total_allocated_mb = (total_used_blocks * BLOCK_SIZE) / (1024 * 1024);
            out += "  已分配: " + std::to_string(total_allocated_mb) + " MB\n";
            out += "  未分配: " + std::to_string(info.size_mb - total_allocated_mb) + " MB\n";
            out += "--------------------------------------\n";
        }
        driver->close();
    }

    if (!found && !target.empty()) {
        return "[-] 错误: 找不到名为 '" + target + "' 的硬盘或分区。";
    }
    return out;
}

// ============================================================
// 🚀 3. 切分新盘（终极逆向锁定盘尾版）
// ============================================================
static std::string drt_disk_split(const std::string& disk_name, const std::string& new_label, const std::string& size_str) {
    DiskHandle h = get_disk_handle(disk_name);
    if (!h.driver) return "[-] 错误: 无法定位磁盘设备。";

    uint32_t size_mb = 0;
    try { size_mb = (uint32_t)std::stoul(size_str); } catch (...) {
        return "[-] 错误: 容量必须是整数。";
    }

    PhysicalLogicalMap pmap{};
    if (!DiskFileSystem::read_partition_table(h.driver, pmap)) {
        pmap.magic = PART_MAGIC;
        pmap.part_count = 0;
    }

    uint32_t size_blocks = (size_mb * 1024 * 1024) / BLOCK_SIZE;
    uint32_t total_sectors = h.driver->get_total_sectors();
    uint32_t end_max = total_sectors - RESERVED_BLOCKS - 1;
    uint32_t data_start = DATA_START_BLOCK_DEF;
    uint32_t total_blocks = end_max - data_start + 1;

    // 🚀 1. 如果切分容量大于等于物理盘总容量，直接全盘变 B
    if (size_blocks >= total_blocks) {
        pmap.magic = PART_MAGIC;
        pmap.part_count = 1;
        strncpy(pmap.parts[0].part_name, new_label.c_str(), 31);
        pmap.parts[0].start_block = data_start;
        pmap.parts[0].end_block   = end_max;
        pmap.parts[0].root_inode  = 2;
        pmap.parts[0].type        = 1;

        DiskFileSystem::write_partition_table(h.driver, pmap);
        return "[+] 切分容量大于等于全盘，全盘已重命名为 '" + new_label + "'。";
    }

    if (pmap.part_count >= MAX_PARTITIONS) {
        return "[-] 错误: 分区已满 (最多8个)";
    }

    // 🚀 2. 核心逻辑：从盘尾向前推算切分点！
    uint32_t target_start = end_max - size_blocks + 1;

    if (pmap.part_count == 0) {
        // 裸盘情况：直接在尾部建 B，然后把前面的全划给 A
        if (target_start - data_start < 1024) {
            return "[-] 错误: 剩余空间不足以建立 A 盘。";
        }

        // 建尾部 B
        strncpy(pmap.parts[0].part_name, new_label.c_str(), 31);
        pmap.parts[0].start_block = target_start;
        pmap.parts[0].end_block   = end_max;
        pmap.parts[0].root_inode  = 2;
        pmap.parts[0].type        = 1;
        pmap.part_count = 1;

        // 建头部 A
        strncpy(pmap.parts[1].part_name, "A", 31);
        pmap.parts[1].start_block = data_start; 
        pmap.parts[1].end_block   = target_start - 1;
        pmap.parts[1].root_inode  = 2;
        pmap.parts[1].type        = 1;
        pmap.part_count++;
    } else {
        // 3. 已有分区情况
        int tail_idx = -1;
        // 找出吃到了盘尾的那个分区
        for (uint32_t i = 0; i < pmap.part_count; ++i) {
            if (pmap.parts[i].end_block >= target_start) {
                tail_idx = i;
                break;
            }
        }

        if (tail_idx != -1) {
            // 找到了尾部占用者，判断切完之后剩下的空间够不够 A 盘
            uint32_t remaining_blocks = target_start - pmap.parts[tail_idx].start_block;
            
            if (remaining_blocks < 1024) {
                // 剩余空间太小（连 4MB 都不到），或者原本就是个全盘占位的裸盘
                // 直接把整个盘变成 B
                pmap.magic = PART_MAGIC;
                pmap.part_count = 1;
                strncpy(pmap.parts[0].part_name, new_label.c_str(), 31);
                pmap.parts[0].start_block = data_start;
                pmap.parts[0].end_block   = end_max;
                pmap.parts[0].root_inode  = 2;
                pmap.parts[0].type        = 1;

                DiskFileSystem::write_partition_table(h.driver, pmap);
                return "[+] 剩余空间不足以建立 A 盘，全盘已重命名为 '" + new_label + "'。";
            }

            // 正常情况：压缩尾部旧分区
            pmap.parts[tail_idx].end_block = target_start - 1;
            if (std::string(pmap.parts[tail_idx].part_name) == "/" || std::string(pmap.parts[tail_idx].part_name) == "[/]") {
                strncpy(pmap.parts[tail_idx].part_name, "A", 31);
            }

            int new_idx = pmap.part_count;
            strncpy(pmap.parts[new_idx].part_name, new_label.c_str(), 31);
            pmap.parts[new_idx].start_block = target_start;
            pmap.parts[new_idx].end_block   = end_max;
            pmap.parts[new_idx].root_inode  = 2;
            pmap.parts[new_idx].type        = 1;
            pmap.part_count++;
        } else {
            // 盘尾本来就是空的，直接在尾部建 B
            int new_idx = pmap.part_count;
            strncpy(pmap.parts[new_idx].part_name, new_label.c_str(), 31);
            pmap.parts[new_idx].start_block = target_start;
            pmap.parts[new_idx].end_block   = end_max;
            pmap.parts[new_idx].root_inode  = 2;
            pmap.parts[new_idx].type        = 1;
            pmap.part_count++;
        }
    }

    DiskFileSystem::write_partition_table(h.driver, pmap);
    return "[+] 成功从盘尾向前切出分区 '" + new_label + "' (" + std::to_string(size_mb) + " MB)";
}

// ============================================================
// 🚀 4. 删除分区（自动找盘）
// ============================================================
static std::string drt_disk_del(const std::string& label) {
    BlockDevice* main_driver = VFS::getInstance().get_disk_driver();
    if (main_driver) {
        PhysicalLogicalMap pmap{};
        if (DiskFileSystem::read_partition_table(main_driver, pmap)) {
            bool found = false;
            for (uint32_t i = 0; i < pmap.part_count; ++i) {
                if (std::string(pmap.parts[i].part_name) == label) {
                    for (uint32_t j = i; j < pmap.part_count - 1; ++j) {
                        pmap.parts[j] = pmap.parts[j + 1];
                    }
                    pmap.part_count--;
                    found = true;
                    break;
                }
            }
            if (found) {
                DiskFileSystem::write_partition_table(main_driver, pmap);
                return "[+] 分区 '" + label + "' 已删除，容量已回归主盘。";
            }
        }
    }

    for (auto& [disk_name, info] : g_virtual_disks) {
        auto driver = std::make_shared<DiskImageDriver>();
        if (!driver->open(info.path)) continue;

        PhysicalLogicalMap pmap{};
        if (!DiskFileSystem::read_partition_table(driver.get(), pmap)) {
            driver->close();
            continue;
        }

        bool found = false;
        for (uint32_t i = 0; i < pmap.part_count; ++i) {
            if (std::string(pmap.parts[i].part_name) == label) {
                for (uint32_t j = i; j < pmap.part_count - 1; ++j) {
                    pmap.parts[j] = pmap.parts[j + 1];
                }
                pmap.part_count--;
                found = true;
                break;
            }
        }

        if (found) {
            DiskFileSystem::write_partition_table(driver.get(), pmap);
            PhysicalLogicalMap empty_plh;
            memset(&empty_plh, 0, sizeof(empty_plh));
            driver->write_sector(PLH_TABLE_BLOCK + pmap.part_count, (uint8_t*)&empty_plh);
            
            driver->close();
            return "[+] 分区 '" + label + "' 已删除，容量已回归总盘。";
        }
        driver->close();
    }
    return "[-] 错误: 找不到该分区。";
}

// ============================================================
// 🚀 5. 重命名分区（自动找盘）
// ============================================================
static std::string drt_disk_rename(const std::string& old_label, const std::string& new_label) {
    BlockDevice* main_driver = VFS::getInstance().get_disk_driver();
    if (main_driver) {
        PhysicalLogicalMap pmap{};
        if (DiskFileSystem::read_partition_table(main_driver, pmap)) {
            bool found = false;
            for (uint32_t i = 0; i < pmap.part_count; ++i) {
                if (std::string(pmap.parts[i].part_name) == old_label) {
                    strncpy(pmap.parts[i].part_name, new_label.c_str(), 31);
                    found = true;
                    break;
                }
            }
            if (found) {
                DiskFileSystem::write_partition_table(main_driver, pmap);
                DiskFileSystem::plh_sync_from_partition(main_driver);
                return "[+] 已将分区 '" + old_label + "' 重命名为 '" + new_label + "'";
            }
        }
    }

    for (auto& [disk_name, info] : g_virtual_disks) {
        auto driver = std::make_shared<DiskImageDriver>();
        if (!driver->open(info.path)) continue;

        PhysicalLogicalMap pmap{};
        if (!DiskFileSystem::read_partition_table(driver.get(), pmap)) {
            driver->close();
            continue;
        }

        bool found = false;
        for (uint32_t i = 0; i < pmap.part_count; ++i) {
            if (std::string(pmap.parts[i].part_name) == old_label) {
                strncpy(pmap.parts[i].part_name, new_label.c_str(), 31);
                found = true;
                break;
            }
        }

        if (found) {
            DiskFileSystem::write_partition_table(driver.get(), pmap);
            DiskFileSystem::plh_sync_from_partition(driver.get());
            driver->close();
            return "[+] 已将分区 '" + old_label + "' 重命名为 '" + new_label + "'";
        }
        driver->close();
    }
    return "[-] 错误: 找不到该分区。";
}

// ============================================================
// 🚀 6. 吞并未分配容量（自动找盘）
// ============================================================
static std::string drt_disk_extend(const std::string& label) {
    BlockDevice* main_driver = VFS::getInstance().get_disk_driver();
    if (main_driver) {
        PhysicalLogicalMap pmap{};
        if (DiskFileSystem::read_partition_table(main_driver, pmap)) {
            uint32_t end_max = main_driver->get_total_sectors() - RESERVED_BLOCKS - 1;
            bool found = false;
            for (uint32_t i = 0; i < pmap.part_count; ++i) {
                if (std::string(pmap.parts[i].part_name) == label) {
                    pmap.parts[i].end_block = end_max;
                    found = true;
                    break;
                }
            }
            if (found) {
                DiskFileSystem::write_partition_table(main_driver, pmap);
                DiskFileSystem::plh_sync_from_partition(main_driver);
                return "[+] 未分配容量已成功吞并给主盘分区 " + label;
            }
        }
    }

    for (auto& [disk_name, info] : g_virtual_disks) {
        auto driver = std::make_shared<DiskImageDriver>();
        if (!driver->open(info.path)) continue;

        PhysicalLogicalMap pmap{};
        if (!DiskFileSystem::read_partition_table(driver.get(), pmap)) {
            driver->close();
            continue;
        }

        uint32_t total_sectors = driver->get_total_sectors();
        uint32_t end_max = total_sectors - RESERVED_BLOCKS - 1;
        bool found = false;

        for (uint32_t i = 0; i < pmap.part_count; ++i) {
            if (std::string(pmap.parts[i].part_name) == label) {
                pmap.parts[i].end_block = end_max;
                found = true;
                break;
            }
        }

        if (found) {
            DiskFileSystem::write_partition_table(driver.get(), pmap);
            DiskFileSystem::plh_sync_from_partition(driver.get());
            driver->close();
            return "[+] 未分配容量已成功吞并给分区 " + label;
        }
        driver->close();
    }
    return "[-] 错误: 找不到该分区。";
}


// ============================================================
// 挂载（🚀 终极防卡死超时版 + 重建 inode 位图 + 挂载点上户口）
// ============================================================
static std::string drt_mount(const std::string& raw_name) {
    std::ofstream log("/data/data/com.termux/files/home/drt_debug.log", std::ios::app);
    log << "\n========== 开始挂载 ==========" << std::endl;

    if (!is_admin_level()) return "[-] 错误: 权限不足，挂载虚拟硬盘需要 R0~R3 权限";

    std::string name = normalize_disk_name(raw_name);
    log << "1. 准备打开镜像: " << name << std::endl;

    auto it = g_virtual_disks.find(name);
    if (it == g_virtual_disks.end()) {
        std::string disk_path = name + ".img";
        if (fs::exists(disk_path)) {
            VirtualDiskInfo info;
            info.name = name; info.path = disk_path;
            info.size_mb = (uint32_t)(fs::file_size(disk_path) / (1024 * 1024));
            info.mounted = false;
            g_virtual_disks[name] = info;
            it = g_virtual_disks.find(name);
        }
    }
    if (it == g_virtual_disks.end()) {
        log << "错误: 硬盘不存在" << std::endl;
        return "[-] 错误: 硬盘 '" + name + "' 不存在。";
    }

    VirtualDiskInfo& info = it->second;
    if (info.mounted) return "[-] 错误: 硬盘 '" + name + "' 已处于挂载状态。";
    if (!fs::exists(info.path)) return "[-] 错误: 物理文件 '" + info.path + "' 不存在。";

    auto driver = std::make_shared<DiskImageDriver>();
    if (!driver->open(info.path)) {
        log << "错误: driver->open 失败" << std::endl;
        return "[-] 错误: 打开镜像文件失败。";
    }
    log << "2. driver->open 成功" << std::endl;

    std::string mount_point = "/dev/" + name;
    log << "3. 准备配置 resolver..." << std::endl;

    auto resolver = [driver](const std::string& sub_path) -> std::shared_ptr<Node> {
        if (sub_path.empty() || sub_path == "/") return std::make_shared<Directory>("", 0);
        DirEntry entry;
        if (!DiskFileSystem::find_entry(driver.get(), sub_path, entry)) return nullptr;
        if (entry.type == 1) {
            uint32_t start_blk = 0;
            InodeEntry ie;
            if (DiskFileSystem::inode_read(driver.get(), entry.inode_id, ie)) {
                start_blk = ie.start_block;
            }
            return std::make_shared<Directory>(entry.name, start_blk);
        }
        return std::make_shared<File>(entry.name);
    };

    log << "4. 准备调用 VFS::mount_disk (看门狗已启动)..." << std::endl;
    log.flush();

    std::atomic<bool> mount_done{false};
    std::atomic<bool> mount_res{false};
    
    std::thread t([&]() {
        DiskFileSystem::rebuild_bitmap(driver.get()); 
        mount_res = VFS::getInstance().mount_disk(mount_point, driver, resolver);
        mount_done = true;
    });

    int waited = 0;
    while (!mount_done && waited < 3000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waited += 100;
    }

    if (!mount_done) {
        t.detach(); 
        log << "错误: 挂载操作发生死锁/死循环，超时返回！" << std::endl;
        return "[-] 错误: 挂载超时！底层磁盘操作阻塞。";
    }
    t.join();

    log << "5. 挂载线程完成，结果为: " << (mount_res.load() ? "true" : "false") << std::endl;

    if (!mount_res.load()) {
        driver->close();
        return "[-] 错误: VFS 挂载失败。";
    }

    {
        ResourceInfo ri;
        ri.type = ResourceType::PUBLIC;
        ri.owner_id = VFS::getInstance().get_effective_owner();
        ri.group_id = -1;
        ri.perms = PERM_ALL;
        VFS::getInstance().set_resource_info(mount_point, ri);
    }

    DiskFileSystem::set_mounted_flag(driver.get(), true);
    info.driver = driver; info.mounted = true; info.mount_point = mount_point;

    log << "6. 挂载流程全部完成！" << std::endl;
    log.close();

    return "[+] 虚拟硬盘 '" + name + "' 已挂载到 " + mount_point;
}

static std::string drt_unmount(const std::string& raw_name) {
    if (!is_admin_level()) return "[-] 错误: 权限不足，卸载虚拟硬盘需要 R0~R3 权限";

    std::string name = normalize_disk_name(raw_name);
    auto it = g_virtual_disks.find(name);
    if (it == g_virtual_disks.end()) return "[-] 错误: 硬盘 '" + name + "' 不存在。";

    VirtualDiskInfo& info = it->second;
    if (!info.mounted) return "[-] 错误: 硬盘 '" + name + "' 未挂载。";

    if (!info.mount_point.empty()) {
        VFS::getInstance().unmount_disk(info.mount_point);
        info.mount_point.clear();
    }
    if (info.driver) {
        DiskFileSystem::set_mounted_flag(info.driver.get(), false);
        info.driver->close();
        info.driver.reset();
    }
    info.mounted = false;
    return "[+] 虚拟硬盘 '" + name + "' 已卸载。";
}

static std::string drt_list_mounted() {
    std::string out = "========== 已挂载的虚拟硬盘 ==========\n";
    int count = 0;
    for (auto& [name, info] : g_virtual_disks) {
        if (!info.mounted) continue;
        out += "  " + name + "  (" + std::to_string(info.size_mb) + " MB)  -> " + info.mount_point + "\n";
        count++;
    }
    if (count == 0) out += "  (暂无已挂载的虚拟硬盘)\n";
    out += "=====================================\n";
    return out;
}

void drt_auto_remount() {
    std::error_code ec;
    auto it = fs::directory_iterator(".", ec);
    if (ec) return;

    for (auto& entry : it) {
        try {
            std::error_code ec2;
            if (!entry.is_regular_file(ec2)) continue;
            std::string fname = entry.path().filename().string();
            if (fname.size() < 4 || fname.substr(fname.size() - 4) != ".img") continue;
            std::string name = fname.substr(0, fname.size() - 4);
            if (name == "disk") continue;

            uint64_t fsize = fs::file_size(entry.path(), ec2);
            if (ec2 || fsize < 1024) continue;

            auto driver = std::make_shared<DiskImageDriver>();
            if (!driver->open(fname)) continue;

            bool was_mounted = false;
            try { was_mounted = DiskFileSystem::get_mounted_flag(driver.get()); } catch (...) { driver->close(); continue; }
            if (!was_mounted) { driver->close(); continue; }

            DiskFileSystem::rebuild_bitmap(driver.get());

            std::string mount_point = "/dev/" + name;

            auto resolver = [driver](const std::string& sub_path) -> std::shared_ptr<Node> {
                if (sub_path.empty() || sub_path == "/") return std::make_shared<Directory>("", 0);
                DirEntry ent;
                if (!DiskFileSystem::find_entry(driver.get(), sub_path, ent)) return nullptr;
                if (ent.type == 1) {
                    uint32_t start_blk = 0; InodeEntry ie;
                    if (DiskFileSystem::inode_read(driver.get(), ent.inode_id, ie)) start_blk = ie.start_block;
                    return std::make_shared<Directory>(ent.name, start_blk);
                }
                return std::make_shared<File>(ent.name);
            };

            if (!VFS::getInstance().mount_disk(mount_point, driver, resolver)) {
                driver->close(); continue;
            }

            {
                ResourceInfo ri;
                ri.type = ResourceType::PUBLIC;
                ri.owner_id = VFS::getInstance().get_effective_owner();
                ri.group_id = -1;
                ri.perms = PERM_ALL;
                VFS::getInstance().set_resource_info(mount_point, ri);
            }

            VirtualDiskInfo info;
            info.name = name; info.path = fname; info.size_mb = (uint32_t)(fsize / (1024 * 1024));
            info.mounted = true; info.mount_point = mount_point; info.driver = driver;
            g_virtual_disks[name] = info;
        } catch (...) { continue; }
    }
}

std::string drt_cmd(DCLState& state, const std::vector<std::string>& args) {
    (void)state;

    if (args.empty()) return drt_usage();

    if (args[0] == "user") {
        std::vector<std::string> sub(args.begin() + 1, args.end());
        return user_cmd(sub);
    }

    if (args[0] == "disk") {
        // 1. 查询主盘：drt disk /
        if (args.size() == 2 && args[1] == "/") {
            return drt_disk_info("/");
        }

        // 2. 查询虚拟盘/分区：drt disk <名字>
        if (args.size() == 2 && args[1] != "/") {
            return drt_disk_info(args[1]);
        }

        // 3. 删除分区：drt disk <分区名> del
        if (args.size() == 3 && args[2] == "del") {
            return drt_disk_del(args[1]);
        }

        // 4. 吞并剩余空间：drt disk <分区名> /
        if (args.size() == 3 && args[2] == "/") {
            return drt_disk_extend(args[1]);
        }

        // 5. 重命名分区：drt disk <旧名> / <新名>
        if (args.size() == 4 && args[2] == "/") {
            return drt_disk_rename(args[1], args[3]);
        }

        // 6. 从主盘切分区：drt disk / <新分区名> <容量MB>
        if (args.size() == 4 && args[1] == "/") {
            return drt_disk_split("/", args[2], args[3]);
        }

        // 7. 从指定虚拟盘切分区：drt disk <虚拟盘名> <新分区名> <容量MB>
        if (args.size() == 4 && args[1] != "/") {
            return drt_disk_split(args[1], args[2], args[3]);
        }

        // 8. 创建新虚拟盘：drt disk <新盘名> <容量MB>
        if (args.size() == 3) {
            return drt_create(args[1], args[2]);
        }
        
        return "[-] 用法错误，请查看 'drt' 帮助。";
    }

    if (args[0] == "img") return drt_list_mounted();

    if (args.size() >= 2) {
        const std::string& name = args[0];
        const std::string& flag = args[1];
        if (flag == "-T") return drt_mount(name);
        if (flag == "-F") return drt_unmount(name);
    }

    return "[-] 未知指令。输入 'drt' 查看用法。";
}