#include "disk_image_driver.h"
#include <iostream>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <mutex>

// 🚀 硬编码，彻底避免宏/常量冲突导致的偏移错乱
static const uint32_t HARD_SECTOR_SIZE = 4096;

// 🚀 每个驱动实例一把锁，保护 file stream 的 seek/read/write 不发生交错
static std::mutex g_driver_mutex;

bool DiskImageDriver::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_driver_mutex);
    
    disk_file.open(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!disk_file.is_open()) return false;

    try {
        uintmax_t size = std::filesystem::file_size(path);
        // 🚀 如果磁盘文件为空，防止除以零和无效扇区
        if (size == 0) {
            total_sectors = 0;
            return false;
        }
        total_sectors = static_cast<uint32_t>((size + HARD_SECTOR_SIZE - 1) / HARD_SECTOR_SIZE);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "[驱动] 获取文件大小失败: " << e.what() << "\n";
        return false;
    }
    return true;
}

void DiskImageDriver::close() {
    std::lock_guard<std::mutex> lock(g_driver_mutex);
    if (disk_file.is_open()) {
        disk_file.flush();
        disk_file.close();
    }
}

bool DiskImageDriver::read_sector(uint32_t sector_num, uint8_t* buffer) {
    if (!buffer) return false;
    
    std::lock_guard<std::mutex> lock(g_driver_mutex); // 🚀 加锁保护
    if (!disk_file.is_open() || sector_num >= total_sectors) return false;

    disk_file.clear();
    disk_file.seekg(static_cast<std::streamoff>(sector_num) * HARD_SECTOR_SIZE, std::ios::beg);
    if (!disk_file.good()) return false;

    disk_file.read(reinterpret_cast<char*>(buffer), HARD_SECTOR_SIZE);
    size_t bytes_read = disk_file.gcount();

    // 🚀 安全清洗：如果发生短读（读到的少于 4096），将剩余部分清零
    if (bytes_read < HARD_SECTOR_SIZE) {
        memset(buffer + bytes_read, 0, HARD_SECTOR_SIZE - bytes_read);
        // 如果连开头都没读到，说明盘坏了
        if (bytes_read == 0) return false;
    }
    return true;
}

bool DiskImageDriver::write_sector(uint32_t sector_num, const uint8_t* buffer) {
    if (!buffer) return false;
    
    std::lock_guard<std::mutex> lock(g_driver_mutex); // 🚀 加锁保护
    if (!disk_file.is_open() || sector_num >= total_sectors) return false;

    disk_file.clear();
    disk_file.seekp(static_cast<std::streamoff>(sector_num) * HARD_SECTOR_SIZE, std::ios::beg);
    if (!disk_file.good()) return false;

    disk_file.write(reinterpret_cast<const char*>(buffer), HARD_SECTOR_SIZE);
    
    // 🚀 建议：如果追求极致性能，可以在这里去掉 flush()
    // 但因为你实现了 journal，元数据写入时上层会 flush，所以这里保留以保证原子写入落盘
    disk_file.flush(); 

    return disk_file.good();
}

void DiskImageDriver::create_image(const std::string& path, uint32_t size_in_mb) {
    // 🚀 防止超出系统文件限制或溢出
    if (size_in_mb == 0 || size_in_mb > 1024 * 1024) { // 限制最大 1TB
        std::cerr << "[驱动] 镜像大小非法: " << size_in_mb << " MB\n";
        return;
    }

    uint64_t total_bytes = static_cast<uint64_t>(size_in_mb) * 1024 * 1024;

    std::ofstream new_disk(path, std::ios::binary | std::ios::trunc);
    new_disk.close();

    std::error_code ec;
    std::filesystem::resize_file(path, total_bytes, ec);
    if (ec) {
        std::cerr << "[驱动] 创建镜像失败: " << ec.message() << "\n";
        std::filesystem::remove(path, ec); // 🚀 失败时清理产生的垃圾文件
        return;
    }

    open(path);
}