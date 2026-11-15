#pragma once
#include "../include/block_device.h"
#include <fstream>
#include <vector>
#include <string>
#include <mutex> // 🚀 引入互斥锁

class DiskImageDriver : public BlockDevice {
private:
    std::fstream disk_file;
    uint32_t total_sectors;
    
    // 🚀 核心修复：每个磁盘驱动拥有自己独立的锁！
    // 这样 A 盘的读写不会阻塞 B 盘的读写。
    mutable std::mutex io_mutex;

public:
    // 🚀 硬编码 4096，和上层 disk_fs.h 的 BLOCK_SIZE 保持一致
    // 使用 static constexpr 让编译器在编译期直接折叠常量
    static constexpr uint32_t SECTOR_SIZE = 4096;

    DiskImageDriver() : total_sectors(0) {}
    ~DiskImageDriver() { close(); }

    // 🚀 核心修复：禁止拷贝，防止底层文件句柄和锁被意外复制
    DiskImageDriver(const DiskImageDriver&) = delete;
    DiskImageDriver& operator=(const DiskImageDriver&) = delete;

    bool open(const std::string& path) override;
    void close() override;
    bool read_sector(uint32_t sector_num, uint8_t* buffer) override;
    bool write_sector(uint32_t sector_num, const uint8_t* buffer) override;

    uint32_t get_total_sectors() const override { return total_sectors; }

    void create_image(const std::string& path, uint32_t size_in_mb);
};