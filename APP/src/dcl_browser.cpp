#include "../include/dcl_browser.h"
#include <string>
#include <iostream>

static std::string map_tag_to_ansi(const std::string& tag) {
    if (tag.rfind("ru=", 0) == 0) {
        std::string hex_str = tag.substr(3);
        if (hex_str.rfind("0x", 0) == 0 || hex_str.rfind("0X", 0) == 0) hex_str = hex_str.substr(2);
        long value = std::strtol(hex_str.c_str(), nullptr, 16);
        if (value <= 0xFF) return "\033[38;5;" + std::to_string(value) + "m";
        int r = (value >> 16) & 0xFF, g = (value >> 8) & 0xFF, b = value & 0xFF;
        return "\033[38;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
    }
    
    // 🚀 核心：只有 <br> 和 <br/> 才换行，其他标签全部抹除！
    if (tag == "br" || tag == "br/") return "\n";
    if (tag == "b")      return "\033[1m";
    if (tag == "/b")     return "\033[0m";
    
    return "";
}

std::string render_xyip_page(const std::string& content) {
    std::string output;
    size_t i = 0;
    while (i < content.size()) {
        if (content[i] == '<') {
            size_t end = content.find('>', i);
            if (end == std::string::npos) {
                output += content[i];
                i++;
                continue;
            }
            std::string tag = content.substr(i + 1, end - i - 1);
            output += map_tag_to_ansi(tag);
            i = end + 1;
            continue;
        }
        
        // 🚀 源码里的物理换行绝对忽略
        if (content[i] == '\n' || content[i] == '\r') {
            i++; 
            continue;
        }
        output += content[i];
        i++;
    }
    
    // 🚀 绝对不补末尾换行，原样返回（哪怕末尾是空格）
    return output;
}