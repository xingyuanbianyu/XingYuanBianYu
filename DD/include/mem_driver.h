#pragma once
#include <cstddef>

// 内存块的结构体，用于内核管理“进程”的虚拟内存
struct VirtualMemoryBlock {
    void* ptr;           // 映射到本进程的起始地址
    size_t size;         // 分配的大小
    int shm_fd;          // Linux/Android 共享内存文件描述符 (Windows下为0)
    void* handle;        // Windows 文件映射句柄 (Linux下为空)
};

// 跨平台内存驱动核心接口
VirtualMemoryBlock* alloc_shared_memory(size_t size);
void free_shared_memory(VirtualMemoryBlock* block);