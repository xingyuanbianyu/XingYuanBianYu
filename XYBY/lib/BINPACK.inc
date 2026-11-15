#pragma once
#include <fstream>
#include <string>
#include <iostream>

// 把 0/1 串打包成真二进制文件
// 传入的 bit_string 必须是 8 位对齐的（7位流请先补零）
bool pack_binary_to_file(const std::string& bits, const std::string& path) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return false;

    unsigned char byte = 0;
    int count = 0;
    for (char c : bits) {
        byte = (byte << 1) | (c == '1' ? 1 : 0);
        if (++count == 8) {
            ofs.put((char)byte);
            byte = 0;
            count = 0;
        }
    }
    // 理论上不会进这里，因为调用前已经对齐了
    if (count > 0) {
        byte <<= (8 - count);
        ofs.put((char)byte);
    }
    ofs.close();
    return true;
}

// 从真二进制文件读出 0/1 串（每字节固定 8 位）
std::string unpack_binary_from_file(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return "";
    std::string bits;
    char ch;
    while (ifs.get(ch)) {
        unsigned char b = (unsigned char)ch;
        for (int i = 7; i >= 0; --i) {
            bits += ((b >> i) & 1) ? '1' : '0';
        }
    }
    ifs.close();
    return bits;
}