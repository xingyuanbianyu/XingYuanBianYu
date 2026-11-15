#include "../include/config.h"
#include "../../VFS/include/vfs.h"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <set>
#include <vector>

// ============================================================
// 默认配置（首次创建时写入，已移除所有外部程序依赖）
// ============================================================
static const char* DEFAULT_CONFIG_CONTENT =
"# ============================================================\n"
"# 星缘编语 · 系统配置\n"
"# 修改后重启生效\n"
"# 格式：key=value（# 开头为注释行）\n"
"# ============================================================\n"
"\n"
"# ---------- 声卡 ----------\n"
"# 底层声卡驱动后端，根据实际编译的 SoundCard 模块决定\n"
"sound.backend=auto\n"
"# 默认音量 0-100\n"
"sound.volume=100\n"
"\n"
"# ---------- 原生播放器 ----------\n"
"# 默认音量\n"
"player.volume=100\n"
"\n"
"# ---------- player 子命令别名 ----------\n"
"player.alias.info=info|状态\n"
"player.alias.help=help|帮助\n"
"player.alias.pause=pause|暂停\n"
"player.alias.resume=resume|继续|恢复\n"
"player.alias.stop=stop|停止\n"
"player.alias.vol=vol|音量\n"
"player.alias.next=next|下一首\n"
"player.alias.prev=prev|上一首\n"
"player.alias.loop=loop|循环|单曲循环|列表循环|关闭循环\n"
"\n"
"# ---------- input 命令 ----------\n"
"input.urls=\n"
"\n"
"# ---------- 系统 ----------\n"
"system.log_level=info\n";

// ============================================================
// 单例
// ============================================================
XybyConfig& XybyConfig::getInstance() {
    static XybyConfig instance;
    return instance;
}

// ============================================================
// 填默认值
// ============================================================
void XybyConfig::applyDefaults() {
    auto def = [this](const std::string& k, const std::string& v) {
        if (m_data.find(k) == m_data.end()) m_data[k] = v;
    };

    def("sound.backend",       "auto");
    def("sound.volume",        "100");
    def("player.volume",       "100");

    // 别名
    def("player.alias.info",   "info|状态");
    def("player.alias.help",   "help|帮助");
    def("player.alias.pause",  "pause|暂停");
    def("player.alias.resume", "resume|继续|恢复");
    def("player.alias.stop",   "stop|停止");
    def("player.alias.vol",    "vol|音量");
    def("player.alias.next",   "next|下一首");
    def("player.alias.prev",   "prev|上一首");
    def("player.alias.loop",   "loop|循环|单曲循环|列表循环|关闭循环");

    def("input.urls",          "");
    def("system.log_level",    "info");
}

// ============================================================
// 解析一行
// ============================================================
void XybyConfig::parseLine(const std::string& line) {
    size_t hash = line.find('#');
    std::string s = (hash == std::string::npos) ? line : line.substr(0, hash);

    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return;
    size_t b = s.find_last_not_of(" \t\r\n");
    s = s.substr(a, b - a + 1);

    size_t eq = s.find('=');
    if (eq == std::string::npos) return;

    std::string key = s.substr(0, eq);
    std::string val = s.substr(eq + 1);

    size_t ka = key.find_first_not_of(" \t");
    if (ka == std::string::npos) return;
    size_t kb = key.find_last_not_of(" \t");
    key = key.substr(ka, kb - ka + 1);

    size_t va = val.find_first_not_of(" \t");
    if (va != std::string::npos) {
        size_t vb = val.find_last_not_of(" \t");
        val = val.substr(va, vb - va + 1);
    } else {
        val = "";
    }

    if (!key.empty()) m_data[key] = val;
}

// ============================================================
// 加载 / 创建 / 补齐（走 VFS）
// ============================================================
void XybyConfig::init(const std::string& path) {
    m_path = path;

    if (!VFS::getInstance().get_node("/")) return;

    size_t slash = m_path.find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
        std::string parent = m_path.substr(0, slash);
        if (!VFS::getInstance().get_node(parent)) {
            VFS::getInstance().create_directory(parent);
        }
    }

    std::set<std::string> keys_in_file;

    if (!VFS::getInstance().get_node(m_path)) {
        VFS::getInstance().create_file(m_path, DEFAULT_CONFIG_CONTENT);

        std::stringstream ss(DEFAULT_CONFIG_CONTENT);
        std::string line;
        while (std::getline(ss, line)) {
            size_t hash = line.find('#');
            std::string t = (hash == std::string::npos) ? line : line.substr(0, hash);
            size_t eq = t.find('=');
            if (eq == std::string::npos) continue;
            std::string k = t.substr(0, eq);
            size_t ka = k.find_first_not_of(" \t");
            if (ka == std::string::npos) continue;
            size_t kb = k.find_last_not_of(" \t");
            keys_in_file.insert(k.substr(ka, kb - ka + 1));
        }
    } else {
        std::string content = VFS::getInstance().read_file(m_path);
        std::stringstream ifs(content);
        std::string line;
        while (std::getline(ifs, line)) {
            size_t hash = line.find('#');
            std::string t = (hash == std::string::npos) ? line : line.substr(0, hash);
            size_t eq = t.find('=');
            if (eq != std::string::npos) {
                std::string k = t.substr(0, eq);
                size_t ka = k.find_first_not_of(" \t");
                if (ka != std::string::npos) {
                    size_t kb = k.find_last_not_of(" \t");
                    keys_in_file.insert(k.substr(ka, kb - ka + 1));
                }
            }
            parseLine(line);
        }
    }

    applyDefaults();

    std::vector<std::string> missing;
    for (const auto& kv : m_data) {
        if (keys_in_file.count(kv.first) == 0) {
            missing.push_back(kv.first + "=" + kv.second);
        }
    }

    if (!missing.empty()) {
        std::string old = VFS::getInstance().read_file(m_path);
        std::string appended = old;
        if (!appended.empty() && appended.back() != '\n') appended += "\n";
        appended += "\n# ---------- 自动补齐 ("
                  + std::to_string(missing.size()) + " 项) ----------\n";
        for (const auto& m : missing) appended += m + "\n";
        VFS::getInstance().write_file(m_path, appended);
    }
}

// ============================================================
// 读取
// ============================================================
std::string XybyConfig::getString(const std::string& key, const std::string& def) const {
    auto it = m_data.find(key);
    if (it == m_data.end()) return def;
    if (it->second.empty()) return def;
    return it->second;
}

int XybyConfig::getInt(const std::string& key, int def) const {
    auto it = m_data.find(key);
    if (it == m_data.end() || it->second.empty()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
}

double XybyConfig::getDouble(const std::string& key, double def) const {
    auto it = m_data.find(key);
    if (it == m_data.end() || it->second.empty()) return def;
    try { return std::stod(it->second); } catch (...) { return def; }
}

bool XybyConfig::getBool(const std::string& key, bool def) const {
    auto it = m_data.find(key);
    if (it == m_data.end() || it->second.empty()) return def;

    std::string v = it->second;
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);

    if (v == "true"  || v == "1" || v == "yes" || v == "on")  return true;
    if (v == "false" || v == "0" || v == "no"  || v == "off") return false;
    return def;
}

void XybyConfig::set(const std::string& key, const std::string& value) {
    m_data[key] = value;
}

// ============================================================
// 保存（走 VFS）
// ============================================================
bool XybyConfig::save() {
    if (!VFS::getInstance().get_node("/")) return false;

    std::vector<std::pair<std::string, std::string>> kv(m_data.begin(), m_data.end());
    std::sort(kv.begin(), kv.end());

    std::string data;
    for (const auto& p : kv) {
        data += p.first + "=" + p.second + "\n";
    }

    if (VFS::getInstance().get_node(m_path)) {
        VFS::getInstance().write_file(m_path, data);
    } else {
        VFS::getInstance().create_file(m_path, data);
    }
    return true;
}

bool XybyConfig::reload() {
    m_data.clear();
    init(m_path);
    return true;
}