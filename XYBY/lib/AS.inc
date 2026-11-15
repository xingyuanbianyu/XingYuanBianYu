// =====================================================================
// lib/AS.inc - 解析嵌套的 AS 关键字节点并执行赋值
// =====================================================================
#include <string>
#include <iostream>
#include <cctype>
#include <map>
#include <vector>

// 提取 AS:{KID:AS VALUE:VID:xxx} 中的 xxx
std::string extractAsVar(const std::string& line) {
    // 【终极定位】使用完整的特征串，绝不计算下标，从根源上杜绝 +10 / +11 的错字！
    std::string tag = "KID:AS VALUE:VID:";
    size_t pos = line.find(tag);
    if (pos == std::string::npos) return "";
    
    pos += tag.length(); // 这一步直接精准定位到变量名的第一个字符
    
    size_t end = pos;
    while (end < line.size() && (isalnum((unsigned char)line[end]) || line[end] == '_')) {
        end++;
    }
    return line.substr(pos, end - pos);
}

// 执行赋值（支持普通变量名和文件路径）
void assignToTarget(const std::string& target, const std::string& value, MemorySpace& mem) {
    if (target.empty()) return;

    // 判断是否为一个文件路径
    if (target.find('/') != std::string::npos || target.find('\\') != std::string::npos) {
        std::cerr << "[AS-DBG] 目标是一个路径: " << target << std::endl;
        std::string file_content = "来自路径的文件内容"; // 替换为真实的 VFS 读取接口
        size_t last_slash = target.find_last_of("/\\");
        std::string var_name = (last_slash == std::string::npos) ? target : target.substr(last_slash + 1);

        Variable v;
        v.type    = "STR";
        v.value   = file_content;
        v.address = allocate_address(var_name);
        if (mem.copy_history.empty()) mem.copy_history.push_back({});
        mem.copy_history.back()[var_name] = v;
        mem.primary_memory[var_name]      = v;
        return;
    }

    // 普通变量赋值
    Variable v;
    v.type    = "STR";
    v.value   = value;
    v.address = allocate_address(target);
    if (mem.copy_history.empty()) mem.copy_history.push_back({});
    mem.copy_history.back()[target] = v;
    mem.primary_memory[target]      = v;
}