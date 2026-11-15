#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

class DCLState;
class DiskImageDriver;

// ============================================================
// 🚀 虚拟硬盘元信息
// ============================================================
struct VirtualDiskInfo {
    std::string name;         // 虚拟硬盘名（用户起的名字）
    std::string path;         // 物理文件路径（如 "disk.img"）
    uint32_t    size_mb = 0;  // 容量（MB）
    bool        mounted = false;
    std::string mount_point = ""; // 🚀 挂载点路径（如 "/dev/mydisk"）
    std::shared_ptr<DiskImageDriver> driver; // 挂载时持有
};

// 全局注册表：name -> VirtualDiskInfo
extern std::map<std::string, VirtualDiskInfo> g_virtual_disks;

// 命令入口
std::string drt_cmd(DCLState& state, const std::vector<std::string>& args);