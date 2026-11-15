#include "../include/amm_memory.h"
#include "../../DCL/include/dcl_kill.h" // 引入内核信号队列接口
#include <iostream>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>
#include <set>
#include <shared_mutex>

AMM& AMM::getInstance() {
    static AMM instance;
    return instance;
}

void AMM::init(size_t max_quota_mb) {
    std::unique_lock<std::shared_mutex> lock(mem_mutex);
    quota = max_quota_mb * 1024 * 1024;
    total_used = 0;
    total_reserved = 0;
    debt_records.clear();
    pointer_to_debt.clear();
    pid_to_debts.clear();
    next_debt_id = 1;
}

// ================= 【核心逻辑：提交申请债】 =================
int AMM::submit_debt(size_t reserve_size, int requester_v_pid, int target_v_pid, 
                     DebtTable table, DebtLevel level, int term_sig, 
                     DebtLifecycle lifecycle, size_t timeout_ms) {
    std::unique_lock<std::shared_mutex> lock(mem_mutex);

    if (reserve_size == 0) return -1;
    if (total_used + total_reserved + reserve_size > quota) {
        return -1; // 额度不够，拒绝
    }

    MemoryDebt debt;
    debt.debt_id = next_debt_id++;
    debt.requester_v_pid = requester_v_pid;
    debt.target_v_pid = target_v_pid;
    debt.termination_signal = term_sig;
    debt.reserved_size = reserve_size;
    debt.committed_size = 0;
    
    // 🚀 核心记录：登记归属表和管理级别
    debt.table = table;
    debt.level = level;
    debt.lifecycle = lifecycle;
    
    debt.v_block = nullptr;
    debt.is_active = true;

    // 🚀 只有“自动表 + 全自动”的债务才需要计算超时
    if (table == DebtTable::AUTO && level == DebtLevel::FULL_AUTO && lifecycle == DebtLifecycle::TIMED) {
        debt.expire_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    } else {
        // 其他情况，过期时间置为最大值，代表永不过期（交由内核或用户手动处理）
        debt.expire_time = std::chrono::steady_clock::time_point::max();
    }

    debt_records[debt.debt_id] = debt;
    total_reserved += reserve_size;

    // 🚀 记录 PID 索引，加速后续清理
    pid_to_debts.insert({debt.requester_v_pid, debt.debt_id});
    if (debt.requester_v_pid != debt.target_v_pid) {
        pid_to_debts.insert({debt.target_v_pid, debt.debt_id});
    }

    return debt.debt_id;
}

// ================= 【核心逻辑：实际提交内存】 =================
void* AMM::commit_memory(int debt_id, size_t actual_size) {
    int target_pid = -1;
    
    // 1. 锁内：检查额度并预扣
    {
        std::unique_lock<std::shared_mutex> lock(mem_mutex);

        auto it = debt_records.find(debt_id);
        if (it == debt_records.end() || !it->second.is_active) return nullptr;

        MemoryDebt& debt = it->second;

        if (actual_size > debt.reserved_size) {
            target_pid = debt.target_v_pid; // 记录目标 PID，准备在锁外发送信号
        } else {
            // 预扣额度
            total_reserved -= debt.reserved_size; 
            total_used += actual_size;
            debt.committed_size = actual_size;
        }
    } // 🚀 释放锁

    // 2. 锁外处理越权
    if (target_pid != -1) {
        std::cerr << "\n[AMM-严重警告] 进程 " << target_pid 
                  << " 试图越过预留额度，执行强制中断！\n";
        kernel_send_signal(std::to_string(target_pid), SIGKILL, "越权触碰预留内存");
        return nullptr;
    }

    // 3. 锁外：真实分配内存（耗时操作）
    VirtualMemoryBlock* v_block = alloc_shared_memory(actual_size);
    
    // 4. 锁内：绑定指针和状态
    {
        std::unique_lock<std::shared_mutex> lock(mem_mutex);
        if (!v_block) {
            // 分配失败，归还预扣的额度
            auto it = debt_records.find(debt_id);
            if (it != debt_records.end()) {
                total_reserved += it->second.reserved_size;
                total_used -= it->second.committed_size;
                it->second.committed_size = 0;
            }
            return nullptr;
        }

        auto it = debt_records.find(debt_id);
        if (it != debt_records.end()) {
            it->second.v_block = v_block;
            pointer_to_debt[v_block->ptr] = debt_id;
        } else {
            // 极端情况：如果在分配期间债务被意外删除了，直接释放
            free_shared_memory(v_block);
            return nullptr;
        }
    }
    return v_block->ptr;
}

// ================= 【定时器触发：内核巡检，只杀全自动】 =================
void AMM::reclaim_expired() {
    struct TerminateRequest {
        int v_pid;
        int sig;
        std::string reason;
    };
    std::vector<TerminateRequest> requests;

    {
        std::unique_lock<std::shared_mutex> lock(mem_mutex);
        auto now = std::chrono::steady_clock::now();
        
        for (auto& [id, debt] : debt_records) {
            // 🚀 核心过滤：内核巡检只管 AUTO 表，且必须是 FULL_AUTO 级别，且已经超时！
            if (debt.table == DebtTable::AUTO && 
                debt.level == DebtLevel::FULL_AUTO && 
                debt.lifecycle == DebtLifecycle::TIMED && 
                now >= debt.expire_time && 
                debt.is_active) 
            {
                debt.is_active = false;
                requests.push_back({debt.target_v_pid, debt.termination_signal, "全自动债务到期"});
            }
            // 半自动(SEMI_AUTO)和手动(MANUAL)：无论有没有超时，内核巡检直接跳过！
        }
    } 

    // 🚀 锁外发送信号
    for (const auto& req : requests) {
        std::cout << "\n[AMM] 定时债务到期，向内核发送信号: 目标 PID " << req.v_pid 
                  << ", 信号 " << req.sig << " (" << req.reason << ")\n";
        kernel_send_signal(std::to_string(req.v_pid), req.sig, req.reason);
    }
}

// ================= 【用户手动关闭：通知 kill 来杀掉】 =================
void AMM::request_termination_by_v_pid(int v_pid) {
    std::vector<int> to_kill_signals;
    
    {
        std::unique_lock<std::shared_mutex> lock(mem_mutex);
        
        // 🚀 性能优化：利用 pid_to_debts 索引 O(logN) 快速查找
        auto range = pid_to_debts.equal_range(v_pid);
        std::set<int> unique_debts;
        for (auto it = range.first; it != range.second; ++it) {
            unique_debts.insert(it->second);
        }

        for (int debt_id : unique_debts) {
            auto dit = debt_records.find(debt_id);
            if (dit != debt_records.end() && dit->second.is_active) {
                dit->second.is_active = false;
                to_kill_signals.push_back(dit->second.termination_signal);
            }
        }
    } 

    // 🚀 锁外发送信号
    for (int sig : to_kill_signals) {
        std::cout << "\n[AMM] 收到用户关闭操作，向内核发送信号: 目标 PID " << v_pid 
                  << ", 信号 " << sig << " (用户手动关闭)\n";
        kernel_send_signal(std::to_string(v_pid), sig, "用户手动关闭");
    }
}

// ================= 【强制回收：由 interrupt_process 反调】 =================
void AMM::reclaim_by_v_pid(int v_pid) {
    std::vector<VirtualMemoryBlock*> blocks_to_free;
    
    {
        std::unique_lock<std::shared_mutex> lock(mem_mutex);
        
        auto range = pid_to_debts.equal_range(v_pid);
        std::set<int> unique_debts;
        for (auto it = range.first; it != range.second; ++it) {
            unique_debts.insert(it->second);
        }

        for (int debt_id : unique_debts) {
            auto it = debt_records.find(debt_id);
            if (it != debt_records.end()) {
                if (it->second.v_block) {
                    total_used -= it->second.committed_size;
                    blocks_to_free.push_back(it->second.v_block);
                    pointer_to_debt.erase(it->second.v_block->ptr);
                } else {
                    total_reserved -= it->second.reserved_size;
                }
                debt_records.erase(it);
            }
        }
        
        // 清理整个 multimap 中属于该 pid 的映射
        auto range_del = pid_to_debts.equal_range(v_pid);
        pid_to_debts.erase(range_del.first, range_del.second);
    }

    // 🚀 锁外执行底层释放，防止死锁
    for (auto* block : blocks_to_free) {
        free_shared_memory(block);
    }
}

void AMM::deallocate(void* user_ptr) {
    if (!user_ptr) return;
    
    VirtualMemoryBlock* block_to_free = nullptr;

    {
        std::unique_lock<std::shared_mutex> lock(mem_mutex);

        auto pit = pointer_to_debt.find(user_ptr);
        if (pit != pointer_to_debt.end()) {
            int debt_id = pit->second;
            auto it = debt_records.find(debt_id);
            if (it != debt_records.end()) {
                total_used -= it->second.committed_size;
                block_to_free = it->second.v_block;
                
                // 清理 pid_to_debts 索引
                auto range1 = pid_to_debts.equal_range(it->second.requester_v_pid);
                for (auto del = range1.first; del != range1.second; ) {
                    if (del->second == debt_id) del = pid_to_debts.erase(del);
                    else ++del;
                }
                auto range2 = pid_to_debts.equal_range(it->second.target_v_pid);
                for (auto del = range2.first; del != range2.second; ) {
                    if (del->second == debt_id) del = pid_to_debts.erase(del);
                    else ++del;
                }

                debt_records.erase(it);
            }
            pointer_to_debt.erase(pit);
        }
    }

    // 🚀 锁外执行底层释放
    if (block_to_free) {
        free_shared_memory(block_to_free);
    }
}

// ================= 【状态查询接口】 =================
void AMM::print_status() {
    std::shared_lock<std::shared_mutex> lock(mem_mutex); 
    
    size_t auto_cnt = 0, manual_cnt = 0;
    for (const auto& pair : debt_records) {
        if (pair.second.table == DebtTable::AUTO) auto_cnt++;
        else manual_cnt++;
    }

    std::cout << "[AMM] 配额: " << (quota / 1024) << " KB, "
              << "实际占用: " << (total_used / 1024) << " KB, "
              << "预留占用: " << (total_reserved / 1024) << " KB, "
              << "活跃债务: " << debt_records.size() << " 笔 "
              << "(自动表: " << auto_cnt << ", 手动表: " << manual_cnt << ")\n";
}

size_t AMM::get_quota() const {
    std::shared_lock<std::shared_mutex> lock(mem_mutex);
    return quota;
}

size_t AMM::get_used() const {
    std::shared_lock<std::shared_mutex> lock(mem_mutex);
    return total_used;
}

size_t AMM::get_reserved() const {
    std::shared_lock<std::shared_mutex> lock(mem_mutex);
    return total_reserved;
}

size_t AMM::get_debt_count() const {
    std::shared_lock<std::shared_mutex> lock(mem_mutex);
    return debt_records.size();
}

size_t AMM::get_auto_table_count() const {
    std::shared_lock<std::shared_mutex> lock(mem_mutex);
    size_t count = 0;
    for (const auto& pair : debt_records) {
        if (pair.second.table == DebtTable::AUTO) count++;
    }
    return count;
}

size_t AMM::get_manual_table_count() const {
    std::shared_lock<std::shared_mutex> lock(mem_mutex);
    size_t count = 0;
    for (const auto& pair : debt_records) {
        if (pair.second.table == DebtTable::MANUAL) count++;
    }
    return count;
}