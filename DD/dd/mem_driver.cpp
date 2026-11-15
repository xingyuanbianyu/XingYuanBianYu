#include "mem_driver.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
    // ================= Windows 平台 =================
    #include <windows.h>
    
    VirtualMemoryBlock* alloc_shared_memory(size_t size) {
        VirtualMemoryBlock* block = new VirtualMemoryBlock();
        block->size = size;
        block->shm_fd = 0;
        block->handle = nullptr;

        HANDLE hMap = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 
            (DWORD)(size >> 32), (DWORD)(size & 0xFFFFFFFF), NULL);
        if (hMap == NULL) { delete block; return nullptr; }

        block->ptr = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, size);
        block->handle = hMap;
        if (block->ptr == NULL) { CloseHandle(hMap); delete block; return nullptr; }
        return block;
    }

    void free_shared_memory(VirtualMemoryBlock* block) {
        if (!block) return;
        if (block->ptr) UnmapViewOfFile(block->ptr);
        if (block->handle) CloseHandle((HANDLE)block->handle);
        delete block;
    }

#elif defined(__linux__) || defined(__ANDROID__)
    // ================= Linux / WSL / Android 平台 =================
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <errno.h>

    VirtualMemoryBlock* alloc_shared_memory(size_t size) {
        VirtualMemoryBlock* block = new VirtualMemoryBlock();
        block->size = size;
        block->shm_fd = -1; // -1 代表匿名 mmap，-2 代表 malloc
        block->handle = nullptr;

        // ================= 1. 首选方案：匿名 mmap =================
        // 这是 Android 和 Linux 上最安全、性能最好的方式，绝对不触发 SIGSYS
        block->ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (block->ptr != MAP_FAILED) {
            return block; // 成功！
        }
        block->ptr = nullptr;

        // ================= 2. 兜底方案：malloc =================
        // 如果 mmap 都失败了（极少见），直接用 C++ 堆分配顶上去
        block->ptr = malloc(size);
        if (block->ptr) {
            block->shm_fd = -2; // 标记为 malloc 分配
            return block;
        }

        // 彻底失败
        delete block;
        return nullptr;
    }

    void free_shared_memory(VirtualMemoryBlock* block) {
        if (!block) return;
        if (block->ptr) {
            if (block->shm_fd == -2) {
                free(block->ptr); // 释放 malloc 的内存
            } else if (block->shm_fd == -1) {
                munmap(block->ptr, block->size); // 释放匿名 mmap 的内存
            }
        }
        delete block;
    }

#endif