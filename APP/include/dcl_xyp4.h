#pragma once
#include <string>
#include <vector>
#include <cstdint>

// 🚀 强制 16 字节对齐的 XYP4 头
#pragma pack(push, 1)
struct Xyp4Header {
    char magic[4];          // "XYP4"
    uint32_t sample_rate;   // 采样率
    uint16_t channels;      // 声道数
    uint16_t bit_depth;     // 位深 (固定 16)
    uint32_t total_samples; // 总采样数
};
#pragma pack(pop)

std::string xyp4_cmd(const std::vector<std::string>& args);