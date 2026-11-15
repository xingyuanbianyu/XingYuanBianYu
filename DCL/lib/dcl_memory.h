#pragma once
#include <cstddef>

// 初始化虚拟内存池（读取 /proc/meminfo 动态计算配额）
void vmem_init();

// 获取当前系统真正可用的内存（字节）
size_t get_system_available_memory();

// 获取我们虚拟内存池的配额上限（字节）
size_t get_vmem_quota();

// 获取当前虚拟内存池已使用的内存（字节）
size_t get_vmem_used();

// 虚拟内存分配（带配额检查）
void* vmem_alloc(size_t size);

// 虚拟内存释放
void vmem_free(void* ptr);