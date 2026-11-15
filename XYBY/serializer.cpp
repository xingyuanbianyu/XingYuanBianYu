#include "include/ast.h"
#include <fstream>
#include <iostream>

class Serializer {
public:
    static void writeToFile(const std::shared_ptr<ASTNode>& root, const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) return;
        file << "{\n";
        serializeNode(file, root, 1);
        file << "\n}\n";
        file.close();
        std::cout << "[构建器] AST 已写入: " << filename << "\n";
    }

private:
    static void serializeNode(std::ostream& out, const std::shared_ptr<ASTNode>& node, int indent) {
        std::string pad(indent * 4, ' ');
        
        // 打印 ID 行
        out << pad << "ID:" << node->inlineCode;
        
        // 只有赋值和判断才需要携带 NID 和 MEID 这种尾部
        // 按照你的手写稿，print 那一行没有 NID，所以这里做一个判断
        if (node->inlineCode.rfind("{CHID:", 0) != 0) {
            out << "NID:" << node->nodeId << "}MEID:LID:" << node->line;
        }

        // 处理嵌套
        if (!node->children.empty()) {
            out << "\n" << pad << "{\n";
            for (size_t i = 0; i < node->children.size(); ++i) {
                serializeNode(out, node->children[i], indent + 1);
                if (i + 1 < node->children.size()) out << "\n";
            }
            out << "\n" << pad << "}";
        }
    }
};