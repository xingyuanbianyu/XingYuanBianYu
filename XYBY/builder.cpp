#include "include/ast.h"
#include "include/char_id_map.h"
#include <iostream>
#include <string>
#include <unordered_map>

class Builder {
private:
    // 变量名 -> 指针地址（确保只分配一次）
    std::unordered_map<std::string, uint32_t> variable_address_map;
    uint32_t next_variable_address = 0x24; // 默认从 0x24 开始，符合你手写示例
    uint32_t next_node_id = 1;

public:
    // 【核心修复】获取变量指针，如果是私有成员直接查表，否则分配一次
    uint32_t get_variable_pointer(const std::string& name) {
        // 1. 优先查私有成员表 (比如 x 默认可能是 0x24，或者其他自定义值)
        auto it_char = CHAR_ID_MAP.find(name);
        if (it_char != CHAR_ID_MAP.end()) return it_char->second;

        // 2. 如果没分配过，分配一个并记录
        auto it_var = variable_address_map.find(name);
        if (it_var != variable_address_map.end()) return it_var->second;

        uint32_t assigned = next_variable_address;
        variable_address_map[name] = assigned;
        next_variable_address += 4; // 每个变量占 4 字节
        return assigned;
    }

    std::shared_ptr<ASTNode> makeNode(const std::string& type, const std::string& val, uint32_t parentId) {
        auto node = std::make_shared<ASTNode>();
        node->nodeType = type; 
        node->value = val;     
        node->parentId = parentId; // 这里的 parentId 在 main 里赋值时会被覆盖
        node->nodeId = next_node_id++;
        return node;
    }
};