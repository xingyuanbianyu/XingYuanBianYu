#include "dcl_memory.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <mutex>
#include <cstdlib>
#include <cstring>

static size_t s_vmem_quota = 0;    // 虚拟内存配额上限（字节）
static size_t s_vmem_used = 0;     // 已使用的虚拟内存（字节）
static std::mutex s_mem_mutex;     // 多线程安全

// 读取 /proc/meminfo 获取系统可用内存
size_t get_system_available_memory() {
    std::ifstream ifs("/proc/meminfo");
    if (!ifs.is_open()) return 0;

    std::string line;
    size_t mem_available_kb = 0;

    while (std::getline(ifs, line)) {
        if (line.rfind("MemAvailable:", 0) == 0) {
            std::stringstream ss(line.substr(13)); // 跳过 "MemAvailable:"
            ss >> mem_available_kb;
            break;
        }
    }
    // 转换为字节 (1 KB = 1024 bytes)
    return mem_available_kb * 1024;
}

// 初始化虚拟内存池
void vmem_init() {
    size_t sys_available = get_system_available_memory();
    
    // 【核心规则】动态计算，只占用剩余可用的一小部分（这里取 10%）
    s_vmem_quota = sys_available / 10;

    // 设置一个最小/最大限制，防止极端情况
    const size_t MIN_QUOTA = 8 * 1024 * 1024;   // 最小 8MB
    const size_t MAX_QUOTA = 128 * 1024 * 1024; // 最大 128MB (可按需调整)

    if (s_vmem_quota < MIN_QUOTA) s_vmem_quota = MIN_QUOTA;
    if (s_vmem_quota > MAX_QUOTA) s_vmem_quota = MAX_QUOTA;

    // 【修改】删除所有的 std::cout 打印，彻底保持静默！
}

size_t get_vmem_quota() { return s_vmem_quota; }
size_t get_vmem_used() { return s_vmem_used; }

// 虚拟内存分配（核心）
void* vmem_alloc(size_t size) {
    std::lock_guard<std::mutex> lock(s_mem_mutex);

    if (size == 0) return nullptr;

    // 检查是否超过动态配额
    if (s_vmem_used + size > s_vmem_quota) {
        // 超出配额，直接拒绝（可以在这里触发清理逻辑，比如清理缓存，或者返回错误）
        // 为了静默，不在终端输出警告，但为了安全，返回 nullptr 让调用者处理
        return nullptr;
    }

    // 【核心改造】采用内存头技术：真实分配大小 = 用户请求大小 + 记录大小(sizeof(size_t))
    size_t total_size = size + sizeof(size_t);
    void* raw_ptr = std::malloc(total_size);
    if (!raw_ptr) return nullptr;

    // 在开头写入用户请求的大小
    *(size_t*)raw_ptr = size;

    // 返回给用户的指针，向后偏移 sizeof(size_t)
    void* user_ptr = (char*)raw_ptr + sizeof(size_t);

    // 记录统计
    s_vmem_used += size;
    
    return user_ptr;
}

// 虚拟内存释放
void vmem_free(void* ptr) {
    if (!ptr) return;

    std::lock_guard<std::mutex> lock(s_mem_mutex);

    // 还原真实的原始指针
    void* raw_ptr = (char*)ptr - sizeof(size_t);
    
    // 读取之前记录的分配大小
    size_t size = *(size_t*)raw_ptr;

    // 扣除统计并释放
    s_vmem_used -= size;
    std::free(raw_ptr);
}