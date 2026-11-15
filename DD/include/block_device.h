#pragma once
#include <cstdint>
#include <string>

const uint32_t SECTOR_SIZE = 512;

class BlockDevice {
public:
    virtual ~BlockDevice() = default;
    virtual bool open(const std::string& path) = 0;
    virtual void close() = 0;
    virtual bool read_sector(uint32_t sector_num, uint8_t* buffer) = 0;
    virtual bool write_sector(uint32_t sector_num, const uint8_t* buffer) = 0;
    
    // 【新增】获取总扇区数，告诉上层这块盘有多大
    virtual uint32_t get_total_sectors() const = 0;
};