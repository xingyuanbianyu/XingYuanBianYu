#ifndef AMM_MEMORY_H
#define AMM_MEMORY_H

#include <shared_mutex>
#include <mutex>
#include <map>
#include <chrono>
#include <vector>
#include <string>
#include <csignal>
#include <memory>
#include "../../DD/include/mem_driver.h"

// ================= 1. 宏观表归属 =================
enum class DebtTable {
    AUTO,   // 自动表：内核进行管理，根据信号处理（什么时候结束、什么时候开始）
    MANUAL  // 手动表：内核只处理怎么回收怎么销毁，生命周期不归内核管
};

// ================= 2. 微观管理级别 =================
enum class DebtLevel {
    FULL_AUTO,  // 全自动：内核全权管理
    SEMI_AUTO,  // 半自动：内核负责大部分（比如分配、回收、越权杀），但声明类型/用途不归内核管
    MANUAL      // 手动：全归用户自己管
};

// 兼容旧的定时枚举，用于 FULL_AUTO
enum class DebtLifecycle {
    TIMED,      
    INDEFINITE  
};

// 内存债务记录结构体
struct MemoryDebt {
    int debt_id;
    int requester_v_pid;
    int target_v_pid;
    int termination_signal;

    size_t reserved_size;
    size_t committed_size;
    
    // 🚀 新增的管理维度
    DebtTable table;
    DebtLevel level;
    DebtLifecycle lifecycle; // 只有 FULL_AUTO + TIMED 才有意义
    
    std::chrono::steady_clock::time_point expire_time;
    
    VirtualMemoryBlock* v_block;
    bool is_active;
};

class AMM {
public:
    static AMM& getInstance();

    void init(size_t max_quota_mb = 256); 
    
    // 🚀 提交申请时，显式声明归属的表和管理级别
    int submit_debt(size_t reserve_size, int requester_v_pid, int target_v_pid, 
                    DebtTable table, DebtLevel level, int term_sig, 
                    DebtLifecycle lifecycle = DebtLifecycle::INDEFINITE, 
                    size_t timeout_ms = 0);
    
    void* commit_memory(int debt_id, size_t actual_size);
    void deallocate(void* user_ptr);
    void reclaim_expired();
    void request_termination_by_v_pid(int v_pid);
    void reclaim_by_v_pid(int v_pid); 
    
    // ================== 状态查询接口 ==================
    size_t get_quota() const;
    size_t get_used() const;
    size_t get_reserved() const;
    size_t get_debt_count() const;
    
    // 🚀 分开统计自动表和手动表
    size_t get_auto_table_count() const;
    size_t get_manual_table_count() const;
    
    void print_status();

private:
    AMM() = default;
    ~AMM() = default;
    AMM(const AMM&) = delete;
    AMM& operator=(const AMM&) = delete;

    mutable std::shared_mutex mem_mutex; 
    
    std::map<int, MemoryDebt> debt_records;  
    std::map<void*, int> pointer_to_debt;   
    std::multimap<int, int> pid_to_debts;   
    
    int next_debt_id = 1;
    
    size_t quota = 0;                  
    size_t total_used = 0;             
    size_t total_reserved = 0;         
};

#endif