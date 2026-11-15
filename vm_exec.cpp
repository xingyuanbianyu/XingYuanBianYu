// ============================================================
// vm_exec.cpp —— 虚拟机字节码执行实现
// 依赖：VM.inc（纯执行体虚拟机）
// ============================================================
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstring>   // memcpy 用

// 🚀 核心：必须引入你的虚拟机头文件，才能认识 Instruction 和 XingYuanVM
#include "lib/VM.inc"

// ============================================================
// execute_bytecode 实现
// 职责：bits(0/1 字符串) → bytes → Instruction 数组 → VM 执行
// 🚀 修改：增加 int io_fd 参数，用于打通前后端输入输出
// ============================================================
bool execute_bytecode(const std::string& code, bool trace, int io_fd) {
    // ---------- 1. 把 0/1 字符串还原成字节流 ----------
    std::vector<uint8_t> bytes;
    unsigned char b = 0;
    int n = 0;
    for (char c : code) {
        b = (unsigned char)((b << 1) | (c == '1' ? 1 : 0));
        if (++n == 8) {
            bytes.push_back(b);
            b = 0;
            n = 0;
        }
    }
    if (n > 0) {           // 末尾不足 8 位，低位补零
        b <<= (8 - n);
        bytes.push_back(b);
    }

    if (trace) {
        std::cerr << "[VM] 收到字节数: " << bytes.size() << "\n";
        std::cerr << "[VM] 原始字节: ";
        for (size_t i = 0; i < bytes.size(); ++i)
            printf("%02X ", bytes[i]);
        std::cerr << "\n";
    }

    // ---------- 2. 字节流 → Instruction 数组 ----------
    std::vector<Instruction> instructions;
    size_t i = 0;
    while (i < bytes.size()) {
        uint8_t op = bytes[i++];
        Instruction inst;
        inst.opcode = static_cast<OpCode>(op);

        // ============================================================
        // 🚀 REG：特殊 2 字节操作数（中断号 + 处理程序 IP）
        //    必须放在通用 1 字节分支之前
        // ============================================================
        if (op == OP_REG) {
            if (i + 2 > bytes.size()) {
                std::cerr << "[VM] 字节流截断: REG 需要 2 字节（中断号 + 处理程序 IP）\n";
                return false;
            }
            inst.operand  = bytes[i++];   // 中断号
            inst.operand2 = bytes[i++];   // 处理程序指令索引
        }
        // ============================================================
        // 带 1 字节操作数的指令
        // 注意：OP_OR / OP_AND / OP_NOT 是无操作数指令，落入 else 分支
        //       OP_REG 已在上面单独处理
        // ============================================================
        else if (op == OP_PUSH || op == OP_JMP || op == OP_GOTO ||
                 op == OP_JUMP_IF_FALSE ||
                 op == OP_LOAD || op == OP_STORE || op == OP_DEL ||
                 op == OP_INT  || op == OP_CALL) {
            if (i < bytes.size()) {
                inst.operand = bytes[i++];
            } else {
                std::cerr << "[VM] 字节流截断: 指令 0x" << std::hex << (int)op
                          << " 缺少操作数\n";
                return false;
            }
        }
        // ============================================================
        // 🚀 PUSH_F：8 字节 double（小端） + 1 字节精度
        // ============================================================
        else if (op == OP_PUSH_F) {
            if (i + 9 > bytes.size()) {
                std::cerr << "[VM] 字节流截断: PUSH_F 需要 9 字节（8 double + 1 prec）\n";
                return false;
            }
            uint64_t bits = 0;
            for (int k = 0; k < 8; ++k) {
                bits |= ((uint64_t)bytes[i + k]) << (8 * k);
            }
            std::memcpy(&inst.fval, &bits, 8);
            inst.fprec = (int)bytes[i + 8];   // 🚀 读精度字节
            i += 9;                            // 🚀 前进 9 字节
        }
        // ============================================================
        // 🚀 PUSH_S：2 字节长度（小端） + N 字节内容
        // ============================================================
        else if (op == OP_PUSH_S) {
            if (i + 2 > bytes.size()) {
                std::cerr << "[VM] 字节流截断: PUSH_S 缺少长度字段\n";
                return false;
            }
            uint16_t len = (uint16_t)bytes[i] | ((uint16_t)bytes[i + 1] << 8);
            i += 2;
            if (i + len > bytes.size()) {
                std::cerr << "[VM] 字节流截断: PUSH_S 内容长度 " << len
                          << " 超出剩余字节 " << (bytes.size() - i) << "\n";
                return false;
            }
            inst.sval.assign(reinterpret_cast<const char*>(&bytes[i]), len);
            i += len;
        }
        // ============================================================
        // 无操作数指令
        // ============================================================
        else {
            inst.operand = 0;
        }
        instructions.push_back(inst);
    }

    if (trace) {
        std::cerr << "[VM] 指令条数: " << instructions.size() << "\n";
    }

    // ---------- 3. 交给纯执行体 VM ----------
    XingYuanVM vm;
    // 🚀 核心修改：把前台的 Socket 链接通道 (io_fd) 交给虚拟机引擎
    vm.run(instructions, io_fd);

    return true;
}