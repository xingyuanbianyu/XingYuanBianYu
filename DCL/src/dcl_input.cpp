#include "dcl_input.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <functional>
#include <set>
#include <map>
#include <algorithm>
#include <cctype>

#include "../../VFS/include/vfs.h"
#include "../../APP/include/http_client.h" 

extern std::string drt_get_name_by_uid(int uid);

std::function<std::string(const std::string&)> g_input_pid_query_cb = nullptr;
std::function<std::string(const std::string&)> g_input_exec_vfs_cb = nullptr;

static const char* VFS_URL_FILE  = "/xyby/data/url.xyt";
static const char* VFS_PKG_DIR   = "/data/packages";
static const char* VFS_REPO_DIR  = "/xyby/repo";
static const char* VFS_INDEX     = "/xyby/repo/index.xyt";
static const char* DEFAULT_URL   = "xy://local/%name%";

struct PackageMeta {
    std::string name;
    std::string version;
    std::string display_name;
    std::string filename;
    std::string url;
};

static bool is_system_level() {
    return static_cast<int>(VFS::getInstance().get_role()) <= static_cast<int>(UserRole::R3_SYS_ADMIN);
}

static bool is_privileged_level() {
    return static_cast<int>(VFS::getInstance().get_role()) <= static_cast<int>(UserRole::R2_HW_ADMIN);
}

static bool is_safe_token(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (std::isalnum((unsigned char)c)) continue;
        if (c == '.' || c == '_' || c == '-' || c == '/' || c == ':' || c == '+' || c == '@') continue;
        return false;
    }
    return true;
}

static void ensure_vfs_dirs() {
    if (!VFS::getInstance().get_node("/xyby")) VFS::getInstance().create_directory("/xyby");
    if (!VFS::getInstance().get_node("/xyby/data")) VFS::getInstance().create_directory("/xyby/data");
    if (!VFS::getInstance().get_node(VFS_REPO_DIR)) VFS::getInstance().create_directory(VFS_REPO_DIR);
    if (!VFS::getInstance().get_node("/data")) VFS::getInstance().create_directory("/data");
    if (!VFS::getInstance().get_node(VFS_PKG_DIR)) VFS::getInstance().create_directory(VFS_PKG_DIR);
    if (!VFS::getInstance().get_node("/input")) VFS::getInstance().create_directory("/input");

    if (!VFS::getInstance().get_node(VFS_URL_FILE)) {
        std::string default_cfg = "# XYBY 可信服务器列表\n# 支持占位符：%name% 或 {{name}}\n" + std::string(DEFAULT_URL) + "\n";
        VFS::getInstance().create_file(VFS_URL_FILE, default_cfg);
    }
}

static std::vector<std::string> load_trusted_urls() {
    std::vector<std::string> urls;
    std::string content = VFS::getInstance().read_file(VFS_URL_FILE);
    if (!content.empty()) {
        std::stringstream ss(content);
        std::string line;
        while (std::getline(ss, line)) {
            size_t a = line.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) continue;
            size_t b = line.find_last_not_of(" \t\r\n");
            line = line.substr(a, b - a + 1);
            if (line.empty() || line[0] == '#') continue;
            urls.push_back(line);
        }
    }
    if (urls.empty()) urls.push_back(DEFAULT_URL);
    return urls;
}

static std::string expand_url_template(const std::string& base, const std::string& pkg_name) {
    static const std::vector<std::string> placeholders = {"%name%", "{{name}}", "${name}", "<name>"};
    for (const auto& ph : placeholders) {
        size_t found = base.find(ph);
        if (found != std::string::npos) {
            std::string full = base;
            size_t pos;
            while ((pos = full.find(ph)) != std::string::npos) {
                full.replace(pos, ph.size(), pkg_name);
            }
            return full;
        }
    }
    std::string full = base;
    if (!full.empty() && full.back() != '/') full += '/';
    full += pkg_name;
    return full;
}

static bool is_xy_protocol(const std::string& url) { return url.rfind("xy://", 0) == 0; }

static bool xy_protocol_fetch(const std::string& url, const std::string& dest) {
    std::string rest = url.substr(5);
    size_t slash = rest.find('/');
    std::string node = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    std::string path = (slash == std::string::npos) ? "" : rest.substr(slash + 1);
    if (node != "local" && !node.empty()) return false;

    std::string vfs_src = std::string(VFS_REPO_DIR) + "/" + path;
    if (!VFS::getInstance().get_node(vfs_src)) return false;

    std::string content = VFS::getInstance().read_file(vfs_src);
    if (content.empty()) return false;

    std::ofstream ofs(dest, std::ios::binary);
    ofs.write(content.data(), content.size());
    return ofs.good();
}

// ============================================================
// 🚀 核心净化：通杀 http 和 https，彻底删掉 curl/wget！
// ============================================================
static bool traditional_download(const std::string& url, const std::string& dest) {
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        std::string err = http_download(url, dest);
        if (!err.empty()) {
            std::cerr << "[NET] 原生下载失败: " << err << "\n";
            return false;
        }
        return true;
    }
    return false;
}

static bool dispatch_download(const std::string& url, const std::string& dest) {
    if (is_xy_protocol(url)) return xy_protocol_fetch(url, dest);
    return traditional_download(url, dest);
}

static bool query_from_servers(const std::string& remote_name, const std::string& local_save, std::vector<std::string>& tried_urls, std::string& out_success_url) {
    auto urls = load_trusted_urls();
    for (const auto& base : urls) {
        std::string full = expand_url_template(base, remote_name);
        tried_urls.push_back(full);

        if (local_save.empty()) {
            if (dispatch_download(full, ".xyby_tmp/tmp_probe")) {
                std::filesystem::remove(".xyby_tmp/tmp_probe");
                out_success_url = full;
                return true;
            }
        } else {
            if (dispatch_download(full, local_save)) {
                out_success_url = full;
                return true;
            }
        }
    }
    return false;
}

static std::string get_host_tmp_file() {
    std::filesystem::create_directories(".xyby_tmp");
    return ".xyby_tmp/input_tmp.bin";
}

static std::string trim_str(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

static bool read_host_file(const std::string& path, std::string& out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return false;
    std::stringstream buf;
    buf << ifs.rdbuf();
    out = buf.str();
    return true;
}

static void force_private_resource(const std::string& vfs_path) {
    if (vfs_path.find("/home/") == 0) {
        ResourceInfo ri;
        ri.type     = ResourceType::PRIVATE;
        ri.owner_id = VFS::getInstance().get_user_id();
        ri.group_id = -1;
        ri.perms    = PERM_ALL;
        VFS::getInstance().set_resource_info(vfs_path, ri);
    }
}

static bool vfs_ensure_dir(const std::string& vfs_path) {
    std::string norm = VFS::getInstance().normalize_path(vfs_path);
    if (norm.empty() || norm == "/") return true;
    std::stringstream ss(norm);
    std::string seg, cur = "";
    while (std::getline(ss, seg, '/')) {
        if (seg.empty()) continue;
        cur += "/" + seg;
        if (!VFS::getInstance().get_node(cur)) {
            std::string err = VFS::getInstance().create_directory(cur);
            if (!err.empty()) {
                if (!VFS::getInstance().get_node(cur)) return false; 
            }
            force_private_resource(cur);
        }
    }
    return true;
}

static std::string host_to_vfs(const std::string& host_file, const std::string& vfs_dest) {
    std::string content;
    if (!read_host_file(host_file, content)) return "无法读取临时文件";
    
    size_t slash = vfs_dest.find_last_of('/');
    if (slash != std::string::npos) {
        if (!vfs_ensure_dir(vfs_dest.substr(0, slash))) {
            return "无法创建父目录: " + vfs_dest.substr(0, slash);
        }
    }

    if (!VFS::getInstance().check_resource_access(vfs_dest)) return "权限不足";

    auto existing = VFS::getInstance().get_node(vfs_dest);
    std::string res;
    if (!existing) {
        res = VFS::getInstance().create_file(vfs_dest, content);
    } else {
        res = VFS::getInstance().write_file(vfs_dest, content);
    }
    
    if (!res.empty()) {
        return "VFS 写入失败: " + res;
    }
    
    force_private_resource(vfs_dest);
    return "";
}

static bool is_binary_content(const std::string& content) {
    if (content.empty()) return false;
    size_t check_len = std::min(content.size(), (size_t)1024);
    for (size_t i = 0; i < check_len; ++i) {
        if (content[i] == '\0') return true;
    }
    return false;
}

static std::map<std::string, PackageMeta> parse_index(const std::string& content) {
    std::map<std::string, PackageMeta> index;
    std::stringstream ss(content);
    std::string line;
    PackageMeta current_meta;
    std::string current_key = "";

    while (std::getline(ss, line)) {
        line = trim_str(line);
        if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            if (!current_key.empty()) index[current_key] = current_meta;
            current_key = line.substr(1, line.size() - 2);
            current_meta = PackageMeta();
            current_meta.name = current_key;
            continue;
        }
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string k = trim_str(line.substr(0, eq));
            std::string v = trim_str(line.substr(eq + 1));
            if (k == "版本") current_meta.version = v;
            else if (k == "名字") current_meta.display_name = v;
            else if (k == "文件名") current_meta.filename = v;
            else if (k == "url") current_meta.url = v;
        }
    }
    if (!current_key.empty()) index[current_key] = current_meta;
    return index;
}

static std::string fetch_index_content() {
    std::string cwd = VFS::getInstance().get_cwd();
    std::string local_index = cwd + "/index.xyt";
    if (cwd.back() == '/') local_index = cwd + "index.xyt";

    if (VFS::getInstance().get_node(local_index)) return VFS::getInstance().read_file(local_index);
    if (VFS::getInstance().get_node(VFS_INDEX)) return VFS::getInstance().read_file(VFS_INDEX);
    return "";
}

// =========================================================================
// 🚀 核心更新：完全套用 dcl_open.cpp 的原生解析逻辑
// =========================================================================
static std::string resolve_path_prefix(const std::string& raw) {
    if (raw.empty()) return raw;
    if (raw == "/") return "/";

    std::string cwd = VFS::getInstance().get_cwd();
    if (cwd.empty()) cwd = "/";

    if (raw == "~") return cwd;
    if (raw == ".") return cwd;

    if (raw.size() >= 2 && raw[0] == '~' && raw[1] == '/') {
        int uid = VFS::getInstance().get_user_id();
        std::string uname = drt_get_name_by_uid(uid);
        std::string home_dir = uname.empty() ? "/" : ("/home/" + uname);
        
        if (home_dir.back() == '/') return home_dir + raw.substr(2);
        return home_dir + raw.substr(1);
    }

    if (raw.size() >= 2 && raw[0] == '.' && raw[1] == '/') {
        if (cwd.back() == '/') return cwd + raw.substr(2);
        return cwd + raw.substr(1);
    }

    return raw;
}

static std::string do_query(const std::string& name, const std::vector<std::string>& extra_args) {
    bool is_direct_url = (name.rfind("http://", 0) == 0 || name.rfind("https://", 0) == 0);
    
    if (!is_direct_url && !is_safe_token(name)) {
        return "错误: 名称包含非法字符";
    }

    std::string host_tmp = get_host_tmp_file();
    std::string open_dir;
    bool do_exec = false;
    
    for (size_t i = 0; i < extra_args.size(); ++i) {
        if (extra_args[i] == "open" && i + 1 < extra_args.size()) {
            open_dir = extra_args[i + 1];
            ++i;
        } else if (extra_args[i] == "exec") {
            do_exec = true;
        }
    }

    if (do_exec && !is_privileged_level()) return "权限不足: 执行宿主机二进制需要 R0~R2 权限";

    std::vector<std::string> tried;
    bool success = false;
    std::string success_url = "";

    if (is_direct_url) {
        tried.push_back(name);
        if (dispatch_download(name, host_tmp)) {
            success = true;
            success_url = name;
        }
    } else {
        success = query_from_servers(name, host_tmp, tried, success_url);
    }

    if (!success) {
        std::cerr << "获取失败，尝试过的地址:\n";
        for (const auto& u : tried) std::cerr << "  " << u << "\n";
        
        std::string manual_url;
        while (true) {
            std::cout << "input>> " << std::flush;
            if (!std::getline(std::cin, manual_url)) return "输入已被取消";
            
            manual_url = trim_str(manual_url);
            if (manual_url.empty()) return "已取消操作";
            
            if (manual_url.rfind("http://", 0) != 0 && manual_url.rfind("https://", 0) != 0) {
                std::cerr << "错误: 必须以 http:// 或 https:// 开头，请重新输入。\n";
                continue;
            }
            
            if (dispatch_download(manual_url, host_tmp)) {
                success = true;
                success_url = manual_url;
                break;
            } else {
                std::cerr << "下载失败，请重新输入，或直接回车取消。\n";
            }
        }
    }

    std::string base_name;
    if (!success_url.empty()) {
        std::string path = success_url;
        size_t scheme_end = path.find("://");
        if (scheme_end != std::string::npos) {
            path = path.substr(scheme_end + 3);
        }
        size_t slash = path.find('/');
        if (slash != std::string::npos) {
            path = path.substr(slash);
            if (path == "/" || path.empty()) {
                base_name = "index.html"; 
            } else {
                size_t last_slash = path.find_last_of('/');
                if (last_slash == path.length() - 1) {
                    base_name = "index.html";
                } else {
                    base_name = path.substr(last_slash + 1);
                }
            }
        } else {
            base_name = "index.html";
        }
    } else {
        base_name = name;
        size_t sl = base_name.find_last_of('/');
        if (sl != std::string::npos) base_name = base_name.substr(sl + 1);
    }

    if (open_dir.empty() && !do_exec) {
        std::string content;
        read_host_file(host_tmp, content);
        std::filesystem::remove(host_tmp);
        if (is_binary_content(content)) return "获取成功，但这是二进制文件，已拦截输出。请使用 open 保存。";
        return content;
    }

    if (open_dir.empty() && do_exec) {
        std::error_code ec;
        std::filesystem::permissions(host_tmp, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add, ec);
        std::string cmd = "\"" + host_tmp + "\"";
        int rc = std::system(cmd.c_str());
        std::filesystem::remove(host_tmp);
        return "执行完成，返回码: " + std::to_string(rc);
    }

    if (!open_dir.empty()) {
        open_dir = resolve_path_prefix(open_dir);
    }

    std::string vfs_dest = open_dir;
    if (vfs_dest.empty()) {
        vfs_dest = base_name;
    } else {
        auto node = VFS::getInstance().get_node(vfs_dest);
        if (node && std::dynamic_pointer_cast<Directory>(node)) {
            if (vfs_dest.back() != '/') vfs_dest += '/';
            vfs_dest += base_name;
        } else if (vfs_dest.back() == '/') {
            vfs_dest += base_name;
        } else {
            if (vfs_dest.find('.') == std::string::npos) {
                vfs_dest += "/" + base_name;
            }
        }
    }

    std::string err = host_to_vfs(host_tmp, vfs_dest);
    std::filesystem::remove(host_tmp);
    if (!err.empty()) return err;

    if (do_exec) {
        if (g_input_exec_vfs_cb) {
            std::string res = g_input_exec_vfs_cb(vfs_dest);
            return "已保存到 VFS: " + vfs_dest + "\n\n[执行结果]:\n" + res;
        }
        return "已保存到 VFS: " + vfs_dest + "\n(未注册执行回调，请手动 exec)";
    }

    return "已保存到 VFS: " + vfs_dest;
}

static std::string get_install_target_dir(const std::string& filename) {
    size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos) return VFS_PKG_DIR; 
    
    std::string ext = filename.substr(dot);
    if (ext == ".xyn") return "/bin/lib";
    if (ext == ".xya" || ext == ".xye") return "/bin/usr";
    if (ext == ".xyo") return "/data";
    if (ext == ".xyd") return "/xyby/data";
    if (ext == ".xyc") return "/config";
    if (ext == ".xyt") return "/text";
    return VFS_PKG_DIR; 
}

static std::string install_package(const std::string& pkg_name) {
    std::string pkg_dir = std::string(VFS_PKG_DIR) + "/" + pkg_name;
    std::string meta_vfs = pkg_dir + "/meta.xyt";

    if (VFS::getInstance().get_node(meta_vfs)) return "包 '" + pkg_name + "' 已经安装过了。";

    std::string index_content = fetch_index_content();
    if (index_content.empty()) return "错误: 本地没有包索引。请先创建 index.xyt。";

    auto index_map = parse_index(index_content);
    auto it = index_map.find(pkg_name);
    if (it == index_map.end()) return "错误: 索引中未找到包 '" + pkg_name + "'。";

    PackageMeta meta = it->second;
    if (meta.url.empty()) return "错误: 该包的元数据中没有配置 url。";
    if (meta.filename.empty()) meta.filename = pkg_name;

    std::string host_tmp = get_host_tmp_file();
    
    if (!dispatch_download(meta.url, host_tmp)) return "下载失败: " + meta.url;

    std::string target_dir = get_install_target_dir(meta.filename);
    vfs_ensure_dir(target_dir);
    
    std::string bin_vfs = target_dir + "/" + meta.filename;
    if (target_dir.back() == '/') bin_vfs = target_dir + meta.filename;
    
    std::string err = host_to_vfs(host_tmp, bin_vfs);
    std::filesystem::remove(host_tmp);
    if (!err.empty()) return err;

    vfs_ensure_dir(pkg_dir);
    std::string meta_content = "包名 = " + meta.name + "\n版本 = " + meta.version + "\n名字 = " + meta.display_name + "\n文件名 = " + meta.filename + "\nurl = " + meta.url + "\n安装路径 = " + bin_vfs + "\n";
    VFS::getInstance().create_file(meta_vfs, meta_content);

    std::string link_path = "/bin/lib/" + pkg_name + ".xyn";
    if (bin_vfs != link_path) {
        std::string link_content = "exec " + bin_vfs + " $!\n";
        VFS::getInstance().create_file(link_path, link_content);
    }

    return "已成功安装 " + pkg_name + " (" + meta.display_name + " " + meta.version + ") -> " + bin_vfs;
}

static std::string remove_package(const std::string& pkg_name) {
    if (!is_system_level()) return "权限不足: 卸载需要 R0~R3 权限";
    std::string pkg_dir = std::string(VFS_PKG_DIR) + "/" + pkg_name;
    std::string meta_vfs = pkg_dir + "/meta.xyt";
    
    if (!VFS::getInstance().get_node(meta_vfs)) return "未安装该包: " + pkg_name;

    std::string meta_content = VFS::getInstance().read_file(meta_vfs);
    auto index_map = parse_index("[" + pkg_name + "]\n" + meta_content);
    auto it = index_map.find(pkg_name);
    
    if (it != index_map.end() && !it->second.filename.empty()) {
        std::string target_dir = get_install_target_dir(it->second.filename);
        std::string real_bin = target_dir + "/" + it->second.filename;
        
        if (VFS::getInstance().get_node(real_bin)) {
            VFS::getInstance().delete_node(real_bin);
        }
    }

    VFS::getInstance().delete_node(pkg_dir);
    
    std::string link_path = "/bin/lib/" + pkg_name + ".xyn";
    if (VFS::getInstance().get_node(link_path)) {
        VFS::getInstance().delete_node(link_path);
    }

    return "已彻底卸载: " + pkg_name;
}

static std::string list_packages() {
    std::string result = "========== 已安装的包 ==========\n";
    auto items = VFS::getInstance().list_directory(VFS_PKG_DIR);
    int count = 0;
    for (auto& raw : items) {
        std::string name = raw;
        if (name.find("[目录] ") == 0) name = name.substr(7);
        std::string meta_vfs = std::string(VFS_PKG_DIR) + "/" + name + "/meta.xyt";
        if (VFS::getInstance().get_node(meta_vfs)) {
            std::string meta = VFS::getInstance().read_file(meta_vfs);
            auto index_map = parse_index("[" + name + "]\n" + meta);
            auto it = index_map.find(name);
            if (it != index_map.end()) result += "  " + name + " \t(" + it->second.display_name + ", v" + it->second.version + ")\n";
            else result += "  " + name + "\n";
            count++;
        }
    }
    if (count == 0) result += "  (暂无已安装的包)\n";
    result += "=================================\n";
    return result;
}

std::string input_cmd(const std::vector<std::string>& args) {
    ensure_vfs_dirs();

    if (args.empty()) {
        return "全能输入工具:\n"
               "  input pid:<pattern>           (查进程)\n"
               "  input <name>                  (获取文本直接打印)\n"
               "  input <name> open <路径>      (获取并保存)\n"
               "  input <name> open <路径> exec (获取、保存并执行)\n"
               "  input install <包名>          (从索引安装包，自动归类)\n"
               "  input remove <包名>           (卸载包)\n"
               "  input list                    (列出已安装包)\n";
    }

    const std::string& first = args[0];

    if (first.rfind("pid:", 0) == 0) {
        std::string pattern = first.substr(4);
        if (g_input_pid_query_cb) return g_input_pid_query_cb(pattern);
        return "未找到匹配的进程。";
    }

    if (first == "list") return list_packages();
    if (first == "remove" || first == "uninstall") {
        if (args.size() < 2) return "用法: input remove <包名>";
        return remove_package(args[1]);
    }
    if (first == "install") {
        if (!is_system_level()) return "权限不足: 安装需要 R0~R3 权限";
        if (args.size() < 2) return "用法: input install <包名>";
        return install_package(args[1]);
    }

    std::string name = args[0];
    std::vector<std::string> extra;
    for (size_t i = 1; i < args.size(); ++i) extra.push_back(args[i]);
    
    return do_query(name, extra);
}