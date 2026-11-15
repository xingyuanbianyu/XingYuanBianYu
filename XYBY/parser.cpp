#include "include/keyword_map.h"
#include "include/symbol_map.h"
#include <iostream>
#include <string>
#include <vector>

struct Token {
    std::string original; 
    std::string opcode;   
    int indentLevel;      
    int line;             
};

class Parser {
public:
    std::vector<Token> parse(const std::string& source) {
        std::vector<Token> tokens;
        int lineNum = 1;
        size_t pos = 0;

        while (pos < source.size()) {
            // 1. 找行尾
            size_t lineEnd = source.find('\n', pos);
            if (lineEnd == std::string::npos) lineEnd = source.size();
            std::string lineStr = source.substr(pos, lineEnd - pos);
            pos = lineEnd + 1;

            // 2. 计算缩进
            int spaces = 0;
            size_t i = 0;
            while (i < lineStr.size() && lineStr[i] == ' ') { spaces++; i++; }
            int indentLevel = spaces / 4;

            // 3. 【核心修复】手动切分，遇到双引号要吃掉一整块
            while (i < lineStr.size()) {
                char c = lineStr[i];
                if (c == ' ') { i++; continue; }
                
                std::string word;
                if (c == '"') {
                    // 字符串：一口气吃到下一个双引号
                    size_t endQuote = lineStr.find('"', i + 1);
                    if (endQuote == std::string::npos) endQuote = lineStr.size() - 1;
                    word = lineStr.substr(i, endQuote - i + 1); // 包含前后双引号
                    i = endQuote + 1;
                } else if (c == '=' && i + 1 < lineStr.size() && lineStr[i+1] == '=') {
                    word = "=="; i += 2;
                } else if (c == ':' || c == '=' || c == '(' || c == ')') {
                    word = std::string(1, c); i++;
                } else {
                    // 普通标识符或数字
                    size_t start = i;
                    while (i < lineStr.size() && lineStr[i] != ' ' && lineStr[i] != '=' && lineStr[i] != ':' && lineStr[i] != '(' && lineStr[i] != ')') { i++; }
                    word = lineStr.substr(start, i - start);
                }
                
                if (word.empty()) continue;

                // 查字典
                std::string opcode = "UNKNOWN";
                auto it_kw = KEYWORD_MAP.find(word);
                if (it_kw != KEYWORD_MAP.end()) opcode = it_kw->second;
                else {
                    auto it_sym = SYMBOL_MAP.find(word);
                    if (it_sym != SYMBOL_MAP.end()) opcode = it_sym->second;
                }
                
                tokens.push_back({word, opcode, indentLevel, lineNum});
            }
            lineNum++;
        }
        return tokens;
    }
};