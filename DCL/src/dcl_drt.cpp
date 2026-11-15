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
    if (u.role == UserRole::R3_SYS_ADMIN) return "/usr/R3.xyt";
    return "/usr/" + u.name + ".xyt";
}

static std::string role_to_perm_type(UserRole r) {
    if (r == UserRole::R3_SYS_ADMIN)     return "xyby";
    if (r == UserRole::R4_GENERAL_ADMIN) return "sudo";
    return "";
}

// ============================================================
// 🚀 核心修复：扩展目录创建逻辑，批量创建专属分类目录
// ============================================================
static void ensure_home_dir(const std::string& name, int uid) {
    // 基础目录检查
    if (!VFS::getInstance().get_node("/home")) {
        VFS::getInstance().create_directory("/home");
    }
    if (!VFS::getInstance().get_node("/ast")) {
        VFS::getInstance().create_directory("/ast");
    }

    // 🚀 1. 创建用户家目录 /home/用户名
    std::string hp = "/home/" + name;
    if (!VFS::getInstance().get_node(hp)) {
        VFS::getInstance().create_directory(hp);
    }
    ResourceInfo ri_home;
    ri_home.type     = ResourceType::PRIVATE;
    ri_home.owner_id = uid;
    ri_home.group_id = -1;
    ri_home.perms    = PERM_ALL;
    VFS::getInstance().set_resource_info(hp, ri_home);

    // 🚀 2. 批量创建用户专属分类目录
    std::vector<std::string> std_dirs = {
        "音乐", "文档", "文本", "图片", "视频", "下载"
    };

    for (const auto& dir_name : std_dirs) {
        std::string dir_path = "/home/" + name + "/" + dir_name;
        
        if (!VFS::getInstance().get_node(dir_path)) {
            VFS::getInstance().create_directory(dir_path);
        }
        
        // 赋予专属权限
        ResourceInfo ri_dir;
        ri_dir.type     = ResourceType::PRIVATE;
        ri_dir.owner_id = uid;
        ri_dir.group_id = -1;
        ri_dir.perms    = PERM_ALL;
        VFS::getInstance().set_resource_info(dir_path, ri_dir);
    }

    // 🚀 3. 创建用户专属回收站 /ast/用户名
    std::string ap = "/ast/" + name;
    if (!VFS::getInstance().get_node(ap)) {
        VFS::getInstance().create_directory(ap);
    }
    ResourceInfo ri_ast;
    ri_ast.type     = ResourceType::PRIVATE;
    ri_ast.owner_id = uid;
    ri_ast.group_id = -1;
    ri_ast.perms    = PERM_ALL;
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
    u.role   = UserRole::R6_GUEST;
    u.source = "星缘编语";
    u.state  = "正常";
    u.uid    = -1;

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
            else if (k == "权限")     { }
            else if (k == "权限来源") u.source = v;
            else if (k == "权限状态") { UserRole r; if (str_to_role(v, r)) u.role = r; }
            continue;
        }

        pos = line.find(':');
        if (pos != std::string::npos) {
            std::string k = line.substr(0, pos);
            std::string v = line.substr(pos + 1);
            if (k == "ID") {
                try { u.uid = std::stoi(v); } catch (...) {}
            }
        }
    }
    if (u.name.empty()) return false;

    g_users[u.name] = u;
    VFS::getInstance().add_protected_path(path);
    VFS::getInstance().set_perm(path, PERM_READ);
    return true;
}

void user_init() {
    if (!VFS::getInstance().get_node("/usr")) {
        VFS::getInstance().create_directory("/usr");
    }

    std::vector<std::string> items = VFS::getInstance().list_directory("/usr");
    for (auto& raw : items) {
        std::string name = strip_color(raw);
        if (name.size() < 4) continue;
        if (name.substr(name.size() - 4) != ".xyt") continue;

        std::string path = "/usr/" + name;
        auto node = VFS::getInstance().get_node(path);
        if (!node) continue;
        if (dynamic_cast<Directory*>(node.get())) continue;

        load_user_from_vfs(path);
        VFS::getInstance().add_protected_path(path);
        VFS::getInstance().set_perm(path, PERM_READ);
    }

    bool has_r3 = false;
    for (auto& [n, u] : g_users)
        if (u.role == UserRole::R3_SYS_ADMIN) { has_r3 = true; break; }

    if (!has_r3) {
        UserAccount u;
        u.name            = "XY";
        u.login_password  = "";
        u.verify_password = "xingyuan";
        u.phone           = "";
        u.email           = "";
        u.role            = UserRole::R3_SYS_ADMIN;
        u.source          = "星缘编语";
        u.state           = "正常";
        u.uid             = 0;
        g_users["XY"] = u;
        save_user_to_vfs(u);
    }

    for (auto& [n, u] : g_users) {
        ensure_home_dir(n, u.uid);
    }

    if (g_users.count("XY")) g_current_user = "XY";
    else if (!g_users.empty()) g_current_user = g_users.begin()->first;

    auto& cur = g_users[g_current_user];
    VFS::getInstance().set_user_id(cur.uid);
    VFS::getInstance().set_role(cur.role);
}

bool drt_verify_r3_password(const std::string& input) {
    for (auto& [name, u] : g_users) {
        if (u.role == UserRole::R3_SYS_ADMIN) {
            return u.verify_password == input;
        }
    }
    return false;
}

std::string drt_get_r3_name() {
    for (auto& [name, u] : g_users) {
        if (u.role == UserRole::R3_SYS_ADMIN) return name;
    }
    return "R3";
}

int drt_get_uid_by_name(const std::string& name) {
    auto it = g_users.find(name);
    if (it != g_users.end()) return it->second.uid;
    return -9999;
}

std::string drt_get_name_by_uid(int uid) {
    for (auto& [name, u] : g_users) {
        if (u.uid == uid) return name;
    }
    return "";
}

static std::string user_cmd(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::string out = "========== 账户列表 ==========\n";
        for (auto& [name, u] : g_users) {
            out += "  " + name + "  (" + role_to_str(u.role)
                 + ", uid=" + std::to_string(u.uid)
                 + ", 来源=" + u.source
                 + ", 状态=" + u.state + ")";
            if (name == g_current_user) out += "  ← 当前";
            out += "\n";
        }
        out += "==============================\n";
        return out;
    }

    if (args[0] == "whoami") {
        auto& u = g_users[g_current_user];
        return "当前账户: " + u.name + "  (" + role_to_str(u.role)
             + ", uid=" + std::to_string(u.uid) + ")";
    }

    if (args[0] == "add") {
        if (!is_admin_level()) {
            return "错误: 权限不足，创建账户需要 R0~R3 权限";
        }
        if (args.size() < 2) return "用法: user add <名字> [登录密码]";
        std::string name = args[1];
        std::string pass = (args.size() >= 3) ? args[2] : "";

        if (g_users.count(name)) return "错误: 账户已存在: " + name;

        UserRole role = UserRole::R5_MEMBER;
        int uid = next_uid_for_role(role);

        UserAccount u;
        u.name            = name;
        u.login_password  = pass;
        u.verify_password = "";
        u.phone           = "";
        u.email           = "";
        u.role            = role;
        u.source          = "星缘编语";
        u.state           = "正常";
        u.uid             = uid;
        g_users[name] = u;
        save_user_to_vfs(u);

        ensure_home_dir(name, uid);

        return "[+] 已创建账户: " + name + "  (R5, uid=" + std::to_string(uid) + ")";
    }

    if (args.size() == 3 && args[0] == "ip") {
        if (args[2] != "-T" && args[2] != "-F") {
            return "用法: user ip <邮箱> -T    设置\n"
                   "      user ip \"\" -F         删除";
        }

        if (!is_admin_level()) {
            return "错误: 需要 R0~R3 权限";
        }

        auto it = g_users.find(g_current_user);
        if (it == g_users.end()) return "错误: 当前账户不存在";

        if (args[2] == "-T") {
            it->second.email = args[1];
            save_user_to_vfs(it->second);
            return "[+] 已设置电子邮箱: " + args[1];
        } else {
            it->second.email = "";
            save_user_to_vfs(it->second);
            return "[+] 已删除电子邮箱";
        }
    }

    if (args.size() == 3 && args[0] == "x手机号") {
        if (args[2] != "-T" && args[2] != "-F") {
            return "用法: user x手机号 <号码> -T    设置\n"
                   "      user x手机号 \"\" -F       删除";
        }

        if (!is_admin_level()) {
            return "错误: 需要 R0~R3 权限";
        }

        auto it = g_users.find(g_current_user);
        if (it == g_users.end()) return "错误: 当前账户不存在";

        if (args[2] == "-T") {
            it->second.phone = args[1];
            save_user_to_vfs(it->second);
            return "[+] 已设置手机号: " + args[1];
        } else {
            it->second.phone = "";
            save_user_to_vfs(it->second);
            return "[+] 已删除手机号";
        }
    }

    if (args.size() == 4 && (args[3] == "-T" || args[3] == "-F")) {
        std::string name    = args[0];
        std::string old_pwd = args[1];
        std::string new_pwd = args[2];
        std::string flag    = args[3];

        bool is_admin = is_admin_level();
        bool is_self  = (name == g_current_user);
        if (!is_admin && !is_self) {
            return "错误: 只能修改自己的密码，或使用管理员身份";
        }

        auto it = g_users.find(name);
        if (it == g_users.end()) return "错误: 账户不存在: " + name;

        if (flag == "-T") {
            if (!it->second.login_password.empty() &&
                it->second.login_password != old_pwd) {
                return "错误: 旧登录密码不正确";
            }
            it->second.login_password = new_pwd;
            save_user_to_vfs(it->second);
            return "[+] 已更新登录密码";
        } else {
            if (!it->second.verify_password.empty() &&
                it->second.verify_password != old_pwd) {
                return "错误: 旧验证密码不正确";
            }
            it->second.verify_password = new_pwd;
            save_user_to_vfs(it->second);
            return "[+] 已更新验证密码";
        }
    }

    if (args.size() == 3 && args[2] == "-T") {
        std::string name   = args[0];
        std::string role_s = args[1];

        if (VFS::getInstance().get_role() != UserRole::R0_ULTIMATE) {
            return "错误: 修改账户权限需要 R0 权限，请使用 'xyby drt user <名字> <权限> -T'";
        }

        UserRole new_role;
        if (!str_to_role(role_s, new_role)) return "错误: 未知角色 " + role_s;
        if (new_role != UserRole::R4_GENERAL_ADMIN &&
            new_role != UserRole::R5_MEMBER &&
            new_role != UserRole::R6_GUEST) {
            return "错误: 只能改成 R4 / R5 / R6";
        }

        auto it = g_users.find(name);
        if (it == g_users.end()) return "错误: 账户不存在: " + name;

        it->second.role = new_role;
        it->second.uid  = next_uid_for_role(new_role);
        save_user_to_vfs(it->second);

        if (name == g_current_user) {
            VFS::getInstance().set_user_id(it->second.uid);
            VFS::getInstance().set_role(it->second.role);
        }

        return "[+] 已将 " + name + " 的权限改为 " + role_to_str(new_role)
             + "  (uid=" + std::to_string(it->second.uid) + ")";
    }

    if (args[0] == "switch" || args[0] == "su") {
        if (args.size() < 2) return "用法: user switch <名字> [登录密码]";
        auto it = g_users.find(args[1]);
        if (it == g_users.end()) return "错误: 账户不存在: " + args[1];

        if (it->second.state != "正常") {
            return "错误: 账户 '" + args[1] + "' 已被禁用";
        }

        if (!it->second.login_password.empty()) {
            std::string input = (args.size() >= 3) ? args[2] : "";
            if (input != it->second.login_password) return "错误: 登录密码不正确";
        }

        g_current_user = args[1];
        VFS::getInstance().set_user_id(it->second.uid);
        VFS::getInstance().set_role(it->second.role);
        return "[+] 已切换到: " + args[1];
    }

    if (args[0] == "del") {
        if (!is_admin_level()) {
            return "错误: 权限不足，删除账户需要 R0~R3 权限";
        }
        if (args.size() < 2) return "用法: user del <名字>";
        std::string name = args[1];
        if (name == "XY")           return "错误: 不能删除默认账户 XY";
        if (name == g_current_user) return "错误: 不能删除当前账户";
        auto it = g_users.find(name);
        if (it == g_users.end()) return "错误: 账户不存在: " + name;

        delete_user_from_vfs(it->second);
        g_users.erase(it);
        return "[+] 已删除账户: " + name;
    }

    return "未知子命令。\n"
           "  user add <名字> [登录密码]\n"
           "  user switch <名字> [登录密码]\n"
           "  user del <名字>\n"
           "  user whoami\n"
           "  user <名字> <R4|R5|R6> -T\n"
           "  user <名字> <旧密码> <新密码> -T    改登录密码\n"
           "  user <名字> <旧密码> <新密码> -F    改验证密码\n"
           "  user ip <邮箱> -T/-F                设置/删除邮箱\n"
           "  user x手机号 <号码> -T/-F            设置/删除手机号";
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
        "========== drt 虚拟硬盘管理 ==========\n"
        "  drt                                      显示此帮助\n"
        "  drt disk <名字> <容量MB>                 创建虚拟硬盘\n"
        "  drt <名字> -T                            挂载虚拟硬盘\n"
        "  drt <已挂载名字> -F                      卸载虚拟硬盘\n"
        "  drt img                                  列出所有已挂载的虚拟硬盘\n"
        "  user                                     列出所有账户\n"
        "  user add <名字> [登录密码]               创建账户 (默认R5)\n"
        "  user switch <名字> [登录密码]            切换账户\n"
        "  user del <名字>                          删除账户\n"
        "  user whoami                              查看当前账户\n"
        "  user <名字> <R4|R5|R6> -T                修改账户权限 (需R0)\n"
        "  user <名字> <旧密码> <新密码> -T         改登录密码\n"
        "  user <名字> <旧密码> <新密码> -F         改验证密码\n"
        "  user ip <邮箱> -T/-F                     设置/删除邮箱\n"
        "  user x手机号 <号码> -T/-F                设置/删除手机号\n"
        "======================================\n";
}

static std::string drt_create(const std::string& raw_name, const std::string& size_str) {
    if (!is_admin_level()) {
        return "[-] 错误: 权限不足，创建虚拟硬盘需要 R0~R3 权限";
    }

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

    DiskFileSystem::format(driver.get(), "/", false);
    driver->close();

    VirtualDiskInfo info;
    info.name    = name;
    info.path    = disk_path;
    info.size_mb = size_mb;
    info.mounted = false;
    g_virtual_disks[name] = info;

    return "[+] 虚拟硬盘 '" + name + "' 已创建，容量 " + std::to_string(size_mb) + " MB，文件: " + disk_path;
}

// ============================================================
// 挂载（🚀 需要 R0~R3 权限）
//    🚀 核心：挂载时必须 rebuild_bitmap，否则从盘 total_blocks=0
// ============================================================
static std::string drt_mount(const std::string& raw_name) {
    if (!is_admin_level()) {
        return "[-] 错误: 权限不足，挂载虚拟硬盘需要 R0~R3 权限";
    }

    std::string name = normalize_disk_name(raw_name);

    auto it = g_virtual_disks.find(name);
    if (it == g_virtual_disks.end()) {
        std::string disk_path = name + ".img";
        if (fs::exists(disk_path)) {
            VirtualDiskInfo info;
            info.name    = name;
            info.path    = disk_path;
            info.size_mb = (uint32_t)(fs::file_size(disk_path) / (1024 * 1024));
            info.mounted = false;
            g_virtual_disks[name] = info;
            it = g_virtual_disks.find(name);
        }
    }
    if (it == g_virtual_disks.end()) return "[-] 错误: 硬盘 '" + name + "' 不存在。";

    VirtualDiskInfo& info = it->second;
    if (info.mounted) return "[-] 错误: 硬盘 '" + name + "' 已处于挂载状态。";
    if (!fs::exists(info.path)) return "[-] 错误: 物理文件 '" + info.path + "' 不存在。";

    auto driver = std::make_shared<DiskImageDriver>();
    if (!driver->open(info.path)) return "[-] 错误: 打开镜像文件失败。";

    // 🚀 【核心修复】挂载时重建该盘的位图 + total_blocks
    DiskFileSystem::rebuild_bitmap(driver.get());

    std::string mount_point = "/dev/" + name;

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

    if (!VFS::getInstance().mount_disk(mount_point, driver, resolver)) {
        driver->close();
        return "[-] 错误: VFS 挂载失败。";
    }

    DiskFileSystem::set_mounted_flag(driver.get(), true);
    info.driver      = driver;
    info.mounted     = true;
    info.mount_point = mount_point;

    return "[+] 虚拟硬盘 '" + name + "' 已挂载到 " + mount_point;
}

// ============================================================
// 卸载（🚀 需要 R0~R3 权限）
// ============================================================
static std::string drt_unmount(const std::string& raw_name) {
    if (!is_admin_level()) {
        return "[-] 错误: 权限不足，卸载虚拟硬盘需要 R0~R3 权限";
    }

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

// ============================================================
// 🚀 启动时静默重挂
// ============================================================
void drt_auto_remount() {
    std::error_code ec;
    auto it = fs::directory_iterator(".", ec);
    if (ec) return;

    for (auto& entry : it) {
        try {
            std::error_code ec2;
            if (!entry.is_regular_file(ec2)) continue;

            std::string fname = entry.path().filename().string();
            if (fname.size() < 4) continue;
            if (fname.substr(fname.size() - 4) != ".img") continue;

            std::string name = fname.substr(0, fname.size() - 4);
            if (name == "disk") continue;

            uint64_t fsize = fs::file_size(entry.path(), ec2);
            if (ec2 || fsize < 1024) continue;

            auto driver = std::make_shared<DiskImageDriver>();
            if (!driver->open(fname)) continue;

            bool was_mounted = false;
            try { was_mounted = DiskFileSystem::get_mounted_flag(driver.get()); }
            catch (...) { driver->close(); continue; }

            if (!was_mounted) { driver->close(); continue; }

            // 🚀 【核心修复】重挂时也要 rebuild_bitmap
            DiskFileSystem::rebuild_bitmap(driver.get());

            std::string mount_point = "/dev/" + name;

            auto resolver = [driver](const std::string& sub_path) -> std::shared_ptr<Node> {
                if (sub_path.empty() || sub_path == "/") return std::make_shared<Directory>("", 0);
                DirEntry ent;
                if (!DiskFileSystem::find_entry(driver.get(), sub_path, ent)) return nullptr;
                if (ent.type == 1) {
                    uint32_t start_blk = 0;
                    InodeEntry ie;
                    if (DiskFileSystem::inode_read(driver.get(), ent.inode_id, ie)) {
                        start_blk = ie.start_block;
                    }
                    return std::make_shared<Directory>(ent.name, start_blk);
                }
                return std::make_shared<File>(ent.name);
            };

            if (!VFS::getInstance().mount_disk(mount_point, driver, resolver)) {
                driver->close();
                continue;
            }

            VirtualDiskInfo info;
            info.name        = name;
            info.path        = fname;
            info.size_mb     = (uint32_t)(fsize / (1024 * 1024));
            info.mounted     = true;
            info.mount_point = mount_point;
            info.driver      = driver;
            g_virtual_disks[name] = info;
        } catch (...) {
            continue;
        }
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
        if (args.size() < 3) return "[-] 用法: drt disk <名字> <容量MB>";
        return drt_create(args[1], args[2]);
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