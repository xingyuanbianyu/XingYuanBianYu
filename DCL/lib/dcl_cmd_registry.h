#ifndef DCL_CMD_REGISTRY_H
#define DCL_CMD_REGISTRY_H

#include <string>
#include <map>

class CommandRegistry {
public:
    static CommandRegistry& getInstance();

    void init();       // 首次调用扫描 PATH，之后短路
    void refresh();    // 强制重扫（用户改了 PATH 时用）

    std::string find(const std::string& cmd_name) const;
    void dump() const;

private:
    CommandRegistry() = default;
    CommandRegistry(const CommandRegistry&) = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;

    std::map<std::string, std::string> cmd_map;
    bool initialized_ = false;    // 【新增】是否已扫描
};

#endif