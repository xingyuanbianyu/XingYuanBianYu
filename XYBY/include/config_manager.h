#pragma once
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <algorithm>

// ==========================================
// 【配置优先级管理器】
// ==========================================
enum class ConfigType {
    // 全局类
    XY_LANG,           // 星缘编语（最高）
    GLOBAL,            // 全局（最低）

    // 集体局部类
    KERNEL,            // 内核
    SYSTEM,            // 系统
    COLLECTIVE_LOCAL,  // 集体局部
    PERMANENT,         // 永久（软件/应用）

    // 单体局部类
    USER,              // 用户
    TEMPORARY,         // 临时
    MONOLITHIC_LOCAL   // 单体局部
};

class ConfigManager {
private:
    // 配置池：配置名 -> { 类型 -> 值 }
    std::map<std::string, std::map<ConfigType, std::string>> m_config_pool;

public:
    // 设置配置
    void setConfig(const std::string& key, const std::string& value, ConfigType type) {
        m_config_pool[key][type] = value;
    }

    // ==========================================
    // 【核心机制】获取配置：严格按照优先级规则查找
    // 优先查系统层（XY_LANG, KERNEL, SYSTEM），绝对不允许用户层覆盖
    // 系统层没配置，才去查用户层的查找链（临时 > 集体局部 > 单体局部 > 全局）
    // ==========================================
    std::string getConfig(const std::string& key, const std::string& default_value = "") {
        if (m_config_pool.find(key) == m_config_pool.end()) return default_value;
        auto& type_map = m_config_pool[key];

        // 【第一梯队：系统级最高优先级】只要有了，直接返回，绝对不走用户查找链
        if (type_map.find(ConfigType::XY_LANG) != type_map.end()) return type_map[ConfigType::XY_LANG];
        if (type_map.find(ConfigType::KERNEL) != type_map.end()) return type_map[ConfigType::KERNEL];
        if (type_map.find(ConfigType::SYSTEM) != type_map.end()) return type_map[ConfigType::SYSTEM];

        // 【第二梯队：用户级查找链】临时 > 集体局部 > 单体局部 > 全局
        std::vector<ConfigType> user_lookup_chain = {
            ConfigType::TEMPORARY,
            ConfigType::COLLECTIVE_LOCAL,
            ConfigType::MONOLITHIC_LOCAL,
            ConfigType::GLOBAL
        };

        for (ConfigType type : user_lookup_chain) {
            if (type_map.find(type) != type_map.end()) {
                return type_map[type];
            }
        }

        return default_value;
    }

    // 【权重映射】用于调试和排序：星缘编语 > 内核 > 系统 > 集体局部 > 用户 > 临时 > 永久 > 全局
    int getPriorityWeight(ConfigType type) {
        switch (type) {
            case ConfigType::XY_LANG:          return 0; // 最高
            case ConfigType::KERNEL:           return 1;
            case ConfigType::SYSTEM:           return 2;
            case ConfigType::COLLECTIVE_LOCAL: return 3;
            case ConfigType::USER:             return 4;
            case ConfigType::TEMPORARY:        return 5;
            case ConfigType::PERMANENT:        return 6;
            case ConfigType::GLOBAL:           return 7; // 最低
            default: return 999;
        }
    }
};