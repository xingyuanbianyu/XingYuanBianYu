#pragma once
#include <string>
#include <map>

// ============================================================
// 系统配置（单例）
//   · 首次启动自动创建 data/config.xyt，写入默认值
//   · 已存在则读取
//   · 改动后需重启生效
// ============================================================
class XybyConfig {
public:
    static XybyConfig& getInstance();

    // 加载或创建
    void init(const std::string& path = "data/config.xyt");

    // ---------- 读 ----------
    std::string getString(const std::string& key, const std::string& def = "") const;
    int         getInt   (const std::string& key, int def = 0) const;
    double      getDouble(const std::string& key, double def = 0.0) const;
    bool        getBool  (const std::string& key, bool def = false) const;

    // ---------- 写（运行时改，需要 save() 才落盘） ----------
    void set(const std::string& key, const std::string& value);

    // 保存到文件
    bool save();

    // 重新加载
    bool reload();

    // 配置文件路径
    std::string path() const { return m_path; }

private:
    XybyConfig() = default;

    void applyDefaults();   // 填默认值
    void parseLine(const std::string& line);

    std::string m_path;
    std::map<std::string, std::string> m_data;
};