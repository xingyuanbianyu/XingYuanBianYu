#pragma once
#include <iostream>
#include <string>
#include "lang_en_map.h"
#include "lang_zh_map.h"
#include "pinyin_map.h"
#include "pinyin_opcode_map.h"

// ==========================================
// 【执行优先级机制】操作码/二进制 > 中英文 > 拼音
// ==========================================
enum class ExecPriority {
    OPCODE_BINARY = 0,   // 最高优先级
    CHINESE_ENGLISH = 1, // 默认起点
    PINYIN = 2           // 最低优先级
};

class ExecPriorityResolver {
public:
    // 【核心解析逻辑】默认从中英文开始，只能往上走，不能往下走
    static std::string resolve(const std::string& token) {
        // 1. 默认从【中英文】层级开始匹配
        if (checkExists(token, ExecPriority::CHINESE_ENGLISH)) {
            return "Resolved from Chinese/English: " + token;
        }

        // 2. 只能向上匹配【操作码和二进制】
        if (checkExists(token, ExecPriority::OPCODE_BINARY)) {
            return "Resolved from Opcode/Binary: " + token;
        }

        // 3. 绝对不允许向下匹配【拼音】！
        return "UNKNOWN";
    }

    // 如果是从拼音入口进入，可以向上匹配中英文和操作码
    static std::string resolveFromPinyin(const std::string& token) {
        if (checkExists(token, ExecPriority::PINYIN)) {
            return "Resolved from Pinyin: " + token;
        }
        if (checkExists(token, ExecPriority::CHINESE_ENGLISH)) {
            return "Resolved from Chinese/English (upward): " + token;
        }
        if (checkExists(token, ExecPriority::OPCODE_BINARY)) {
            return "Resolved from Opcode/Binary (upward): " + token;
        }
        return "UNKNOWN";
    }

private:
    static bool checkExists(const std::string& token, ExecPriority level) {
        // 1. 操作码和二进制层
        if (level == ExecPriority::OPCODE_BINARY) {
            if (token.find("0x") == 0 || (!token.empty() && isdigit(token[0]))) return true;
            // 也可以在这里加查 opcode_binary_map.h 的逻辑
        }
        
        // 2. 中英文层
        if (level == ExecPriority::CHINESE_ENGLISH) {
            if (LANG_EN_MAP.find(token) != LANG_EN_MAP.end()) return true;
            if (LANG_ZH_MAP.find(token) != LANG_ZH_MAP.end()) return true;
        }
        
        // 3. 拼音层
        if (level == ExecPriority::PINYIN) {
            if (PINYIN_MAP.find(token) != PINYIN_MAP.end()) return true;
            if (PINYIN_OPCODE_MAP.find(token) != PINYIN_OPCODE_MAP.end()) return true;
        }

        return false;
    }
};