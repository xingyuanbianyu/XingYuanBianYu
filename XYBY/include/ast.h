#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

struct ASTNode {
    std::string nodeType;   // 例如 KID, VID, FID, CHID, PROGRAM
    std::string value;      // 例如 IF, x, PRINT, "Hello World"
    std::string inlineCode; // 【核心】专门用来装按行压缩组装好的代码
    uint32_t parentId = 0;  
    uint32_t nodeId = 0;    
    uint32_t line = 1;      
    std::vector<std::shared_ptr<ASTNode>> children;
};