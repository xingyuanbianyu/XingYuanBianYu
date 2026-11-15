// ============================================================
// vm_reg_exec.cpp —— 寄存器式虚拟机字节码执行实现
// 依赖：lib/VM_REG.inc
// ============================================================
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>

#include "lib/VM_REG.inc"

// 🚀 修改：增加 int io_fd 参数，用于打通前后端输入输出
bool execute_bytecode_register(const std::string& bits, bool trace, int io_fd) {
    // bits → bytes
    std::vector<uint8_t> bytes;
    unsigned char b = 0;
    int n = 0;
    for (char c : bits) {
        b = (unsigned char)((b << 1) | (c == '1' ? 1 : 0));
        if (++n == 8) { bytes.push_back(b); b = 0; n = 0; }
    }
    if (n > 0) { b <<= (8 - n); bytes.push_back(b); }

    if (trace) {
        std::cerr << "[REG-VM] 字节数: " << bytes.size() << "\n";
    }

    XingYuanRegVM vm;
    // 🚀 核心修改：把前台的 Socket 链接通道 (io_fd) 交给寄存器虚拟机引擎
    vm.run(bytes, io_fd);
    
    return true;
}