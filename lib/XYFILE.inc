// ============================================================
// lib/XYFILE.inc —— 通用文件读写（白名单 + 24 字节头）
// 扩展名白名单：.xy / .xya / .xye / .xyb
// ============================================================
#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <cstdint>

// ============================================================
// 扩展名白名单
// ============================================================
inline bool xy_allowed_ext(const std::string& ext) {
    return ext == ".xy" || ext == ".xya" || ext == ".xye" || ext == ".xyb";
}

// ============================================================
// 三态转换
// ============================================================
inline std::string xy_void_to_bits(const std::string& v) {
    std::string bits;
    int n = 0;
    for (char c : v) {
        if (c == '0') n++;
        else if (c == '.' || c == ',') {
            if (n == 1) bits += '0';
            else if (n == 2) bits += '1';
            n = 0;
        }
    }
    return bits;
}

inline std::string xy_bits_to_bytes(const std::string& bits) {
    std::string bytes;
    unsigned char b = 0;
    int n = 0;
    for (char c : bits) {
        b = (unsigned char)((b << 1) | (c == '1' ? 1 : 0));
        if (++n == 8) { bytes += (char)b; b = 0; n = 0; }
    }
    if (n > 0) { b <<= (8 - n); bytes += (char)b; }
    return bytes;
}

inline std::string xy_bytes_to_bits(const std::string& bytes, size_t valid) {
    std::string bits;
    for (char ch : bytes) {
        unsigned char b = (unsigned char)ch;
        for (int i = 7; i >= 0; --i) bits += ((b >> i) & 1) ? '1' : '0';
    }
    if (valid > 0 && bits.size() > valid) bits = bits.substr(0, valid);
    return bits;
}

inline std::string xy_bits_to_void(const std::string& bits, int group = 7) {
    std::string out;
    for (size_t i = 0; i < bits.size(); ++i) {
        out += (bits[i] == '0') ? "0" : "00";
        bool last = (i + 1 == bits.size());
        bool group_end = ((i + 1) % group == 0);
        if (last)            out += ",";
        else if (group_end)  out += ",";
        else                 out += ".";
    }
    return out;
}

// ============================================================
// 通用写文件
// 参数：
//   dir   目标目录（比如 g_user_cwd）
//   name  文件名（不含扩展名）
//   ext   扩展名，必须在白名单里
//   bits  中间态数据（0/1 串）
//   group 分组参数 7 或 8
// 返回：完整路径（成功）或空字符串（失败）
// ============================================================
inline std::string xyfile_write(const std::string& dir,
                                 const std::string& name,
                                 const std::string& ext,
                                 const std::string& bits,
                                 int group = 7,
                                 const std::string& owner = "XingYuan",
                                 const std::string& version = "v1.0.0",
                                 const std::string& magic = "XYBY") {
    // 1. 扩展名白名单
    if (!xy_allowed_ext(ext)) {
        std::cerr << "[XYFILE] 拒绝: 扩展名 " << ext 
                  << " 不在白名单 (.xy/.xya/.xye/.xyb)\n";
        return "";
    }

    // 2. 拼接路径
    std::string full = dir.empty() ? (name + ext) : (dir + "/" + name + ext);

    // 3. 中间态 → 存储态
    std::string bytes = xy_bits_to_bytes(bits);

    // 4. 写文件
    std::ofstream ofs(full, std::ios::binary);
    if (!ofs.is_open()) {
        std::cerr << "[XYFILE] 无法打开: " << full << "\n";
        return "";
    }

    // 归属 9 字节
    char b9[9] = {0};
    strncpy(b9, owner.c_str(), 9);
    ofs.write(b9, 9);

    // 版本 6 字节
    char b6[6] = {0};
    strncpy(b6, version.c_str(), 6);
    ofs.write(b6, 6);

    // 魔数 5 字节
    char b5[5] = {0};
    strncpy(b5, magic.c_str(), 5);
    ofs.write(b5, 5);

    // 有效位数 3 字节（小端）
    uint32_t vb = (uint32_t)bits.size();
    if (vb > 0xFFFFFF) vb = 0xFFFFFF;
    uint8_t vb3[3];
    vb3[0] = (uint8_t)(vb & 0xFF);
    vb3[1] = (uint8_t)((vb >> 8) & 0xFF);
    vb3[2] = (uint8_t)((vb >> 16) & 0xFF);
    ofs.write((char*)vb3, 3);

    // 分组 1 字节
    uint8_t g = (uint8_t)group;
    ofs.write((char*)&g, 1);

    // 数据
    ofs.write(bytes.c_str(), bytes.size());
    ofs.close();

    std::cerr << "[XYFILE] 写入: " << full 
              << " (" << bits.size() << " 位 -> " 
              << bytes.size() << " 字节)\n";
    return full;
}

// ============================================================
// 通用读文件
// ============================================================
struct XYReadResult {
    bool ok = false;
    std::string owner;
    std::string version;
    std::string magic;
    uint32_t valid_bits = 0;
    uint8_t group = 7;
    std::string bits;       // 中间态
    std::string void_data;  // 本态
};

inline XYReadResult xy_read_file(const std::string& path,
                                 const std::string& expect_magic = "") {
    XYReadResult r;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "[XYFILE] 无法打开: " << path << "\n";
        return r;
    }

    char buf[9] = {0};
    ifs.read(buf, 9);
    r.owner = std::string(buf, strnlen(buf, 9));

    char ver[6] = {0};
    ifs.read(ver, 6);
    r.version = std::string(ver, strnlen(ver, 6));

    char mg[5] = {0};
    ifs.read(mg, 5);
    r.magic = std::string(mg, strnlen(mg, 5));

    if (!expect_magic.empty() && r.magic != expect_magic) {
        std::cerr << "[XYFILE] 魔数不匹配: 期望 " << expect_magic 
                  << " 实际 " << r.magic << "\n";
        return r;
    }

    uint8_t vb3[3];
    ifs.read((char*)vb3, 3);
    r.valid_bits = vb3[0] | (vb3[1] << 8) | (vb3[2] << 16);

    ifs.read((char*)&r.group, 1);

    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string bytes = ss.str();
    ifs.close();

    r.bits = xy_bytes_to_bits(bytes, r.valid_bits);
    r.void_data = xy_bits_to_void(r.bits, r.group);
    r.ok = true;
    return r;
}