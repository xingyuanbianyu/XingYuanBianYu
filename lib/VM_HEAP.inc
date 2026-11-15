#ifndef XY_VM_HEAP_H
#define XY_VM_HEAP_H
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include "VM.inc"

enum HeapOpCode {
    OP_ALLOC = 0x50,
    OP_REF = 0x51,
    OP_FREE = 0x52,
    OP_DEREF = 0x53,
    OP_H_MOV_IMM = 0x54,
    OP_H_MOV = 0x55,
    OP_H_MOV_F = 0x56,
    OP_H_MOV_S = 0x57
};

class XingYuanHeapVM {
private:
    static constexpr size_t REG_COUNT = 256;
    static constexpr size_t MAX_EXEC = 100000000;
    static constexpr int TIMEOUT_INT = 0;
    static constexpr int DIVZERO_INT = 1;
    static constexpr int DIRTY_INT = 3;
    static constexpr uint32_t PHYS_BASE = 0x100;
    static constexpr uint32_t VIRT_BASE = 0x10000000;
    static constexpr uint32_t DEFAULT_QUOTA = 4096;

    struct Slot {
        Value value;
        int ref = 0;
        bool alive = false;
        bool is_virtual = false;
    };

    std::vector<Value> regs;
    std::unordered_map<char, Value> variables;
    
    // 🚀 记录变量的强类型，防止重复定义不同类型
    std::unordered_map<char, Value::Type> var_types;

    std::vector<Slot> physical;
    std::vector<Slot> virtual_heap;
    std::vector<uint32_t> free_list;
    uint32_t next_phys = PHYS_BASE;
    uint32_t next_virt = VIRT_BASE;
    uint32_t quota = DEFAULT_QUOTA;
    int io_fd = -1;

    Slot* resolve(uint32_t addr) {
        if (addr >= VIRT_BASE) {
            uint32_t i = addr - VIRT_BASE;
            return (i < virtual_heap.size()) ? &virtual_heap[i] : nullptr;
        }
        if (addr < PHYS_BASE) return nullptr;
        uint32_t i = addr - PHYS_BASE;
        return (i < physical.size()) ? &physical[i] : nullptr;
    }

    uint32_t heap_alloc(const Value& v) {
        if (!free_list.empty()) {
            uint32_t a = free_list.back(); free_list.pop_back();
            physical[a - PHYS_BASE] = {v, 0, true, false};
            return a;
        }
        if (physical.size() < quota) {
            uint32_t a = next_phys++;
            physical.push_back({v, 0, true, false});
            return a;
        }
        uint32_t a = next_virt++;
        virtual_heap.push_back({v, 0, true, true});
        return a;
    }

    void heap_incref(uint32_t addr) {
        Slot* s = resolve(addr);
        if (s && s->alive) s->ref++;
    }

    void heap_decref(uint32_t addr) {
        Slot* s = resolve(addr);
        if (!s || !s->alive) return;
        if (--s->ref <= 0) {
            s->alive = false;
            if (!s->is_virtual) free_list.push_back(addr);
        }
    }

    Value heap_deref(uint32_t addr) {
        int guard = 100;
        while (guard-- > 0) {
            Slot* s = resolve(addr);
            if (!s || !s->alive) return Value(0);
            if (s->value.type == Value::ADDR) { addr = s->value.addr; continue; }
            return s->value;
        }
        return Value(0);
    }

    bool check_clean() const {
        for (const auto& s : physical) if (s.alive) return false;
        return true;
    }

    std::unordered_map<int, size_t> interrupts;
    std::vector<size_t> call_stack;
    size_t ip = 0;

    bool raise_interrupt(int intno) {
        auto it = interrupts.find(intno);
        if (it == interrupts.end()) return false;
        call_stack.push_back(ip);
        ip = it->second;
        return true;
    }

    void print_error(const std::string& msg) {
        if (io_fd != -1) {
            write(io_fd, msg.c_str(), msg.size());
        } else {
            std::cerr << msg;
        }
    }

public:
    XingYuanHeapVM() : regs(REG_COUNT) {}

    void run(const std::vector<uint8_t>& bytecode, int fd = -1) {
        io_fd = fd;
        ip = 0;
        size_t exec_count = 0;

        while (ip < bytecode.size()) {
            if (++exec_count > MAX_EXEC) {
                if (raise_interrupt(TIMEOUT_INT)) { exec_count = 0; continue; }
                print_error("\n[HEAP-VM] 执行超时，强制退出\n");
                return;
            }

            uint8_t op = bytecode[ip++];
            switch (op) {
                case OP_ALLOC: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++];
                    uint32_t a = heap_alloc(regs[ra]);
                    heap_incref(a);
                    regs[rd] = Value(a);
                    break;
                }
                case OP_REF: {
                    uint8_t ra = bytecode[ip++];
                    if (regs[ra].type == Value::ADDR) heap_incref(regs[ra].addr);
                    break;
                }
                case OP_FREE: {
                    uint8_t ra = bytecode[ip++];
                    if (regs[ra].type == Value::ADDR) heap_decref(regs[ra].addr);
                    break;
                }
                case OP_DEREF: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++];
                    if (regs[ra].type == Value::ADDR) regs[rd] = heap_deref(regs[ra].addr);
                    else regs[rd] = regs[ra];
                    break;
                }
                case OP_H_MOV_IMM: {
                    uint8_t rd = bytecode[ip++], imm = bytecode[ip++];
                    regs[rd] = Value((int)imm);
                    break;
                }
                case OP_H_MOV: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++];
                    regs[rd] = regs[ra];
                    break;
                }
                case OP_H_MOV_F: {
                    uint8_t rd = bytecode[ip++];
                    double fval = 0.0;
                    std::memcpy(&fval, &bytecode[ip], 8); ip += 8;
                    uint8_t prec = bytecode[ip++];
                    regs[rd] = Value(fval, (int)prec);
                    break;
                }
                case OP_H_MOV_S: {
                    uint8_t rd = bytecode[ip++];
                    uint16_t len = bytecode[ip] | (bytecode[ip + 1] << 8); ip += 2;
                    std::string sval(bytecode.begin() + ip, bytecode.begin() + ip + len); ip += len;
                    regs[rd] = Value(sval);
                    break;
                }
                case OP_TRUE: {
                    uint8_t rd = bytecode[ip++];
                    regs[rd] = Value(1);
                    break;
                }
                case OP_FALSE: {
                    uint8_t rd = bytecode[ip++];
                    regs[rd] = Value(0);
                    break;
                }
                case OP_LOAD: {
                    uint8_t rd = bytecode[ip++];
                    char var = (char)bytecode[ip++];
                    auto it = variables.find(var);
                    if (it != variables.end()) {
                        regs[rd] = it->second;
                    } else {
                        print_error("[HeapVM Error] 未定义变量\n");
                        return;
                    }
                    break;
                }
                
                // 🚀 核心修改：堆式 OP_STORE 严格类型校验
                case OP_STORE: {
                    char var = (char)bytecode[ip++];
                    uint8_t ra = bytecode[ip++];
                    Value addr_val = regs[ra];
                    Value actual_val = (addr_val.type == Value::ADDR) ? heap_deref(addr_val.addr) : addr_val;

                    auto it = var_types.find(var);
                    if (it == var_types.end()) {
                        var_types[var] = actual_val.type;
                    } else {
                        // 如果类型不匹配，直接中断，防止脏数据继续传播
                        if (it->second != actual_val.type && actual_val.type != Value::ADDR) {
                            print_error("[Runtime Error] 强类型冲突：变量类型不能改变。\n");
                            return;
                        }
                    }
                    variables[var] = addr_val;
                    break;
                }
                
                // 🚀 核心修改：所有算术运算遇到类型不匹配，直接返回（熔断）
                case OP_ADD: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++], rb = bytecode[ip++];
                    Value a = regs[ra], b = regs[rb];
                    if (a.type == Value::STRING && b.type == Value::STRING) {
                        regs[rd] = Value(a.s + b.s);
                    } else if (a.type == Value::INT && b.type == Value::INT) {
                        regs[rd] = Value(BigInt::add(a.big, b.big));
                    } else if (both_numeric(a, b)) {
                        regs[rd] = Value(to_double(a) + to_double(b), merge_prec(a, b));
                    } else {
                        print_error("[Runtime Error] ADD 类型不匹配\n");
                        return; 
                    }
                    break;
                }
                case OP_SUB: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++], rb = bytecode[ip++];
                    Value a = regs[ra], b = regs[rb];
                    if (!both_numeric(a, b)) {
                        print_error("[Runtime Error] SUB 需要数值\n");
                        return;
                    }
                    if (a.type == Value::INT && b.type == Value::INT)
                        regs[rd] = Value(BigInt::sub(a.big, b.big));
                    else
                        regs[rd] = Value(to_double(a) - to_double(b), merge_prec(a, b));
                    break;
                }
                case OP_MUL: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++], rb = bytecode[ip++];
                    Value a = regs[ra], b = regs[rb];
                    if (!both_numeric(a, b)) {
                        print_error("[Runtime Error] MUL 需要数值\n");
                        return;
                    }
                    if (a.type == Value::INT && b.type == Value::INT)
                        regs[rd] = Value(BigInt::mul(a.big, b.big));
                    else
                        regs[rd] = Value(to_double(a) * to_double(b), merge_prec(a, b));
                    break;
                }
                case OP_DIV: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++], rb = bytecode[ip++];
                    Value a = regs[ra], b = regs[rb];
                    if (!both_numeric(a, b)) {
                        print_error("[Runtime Error] DIV 需要数值\n");
                        return;
                    }
                    if (a.type == Value::INT && b.type == Value::INT) {
                        if (b.big.is_zero()) {
                            if (!raise_interrupt(DIVZERO_INT)) {
                                print_error("[HEAP-VM] 除零错误，无处理程序\n");
                                return;
                            }
                            break;
                        }
                        regs[rd] = Value(BigInt::div(a.big, b.big));
                    } else {
                        double db = to_double(b);
                        if (db == 0.0) {
                            if (!raise_interrupt(DIVZERO_INT)) {
                                print_error("[HEAP-VM] 除零错误，无处理程序\n");
                                return;
                            }
                            break;
                        }
                        regs[rd] = Value(to_double(a) / db, merge_prec(a, b));
                    }
                    break;
                }
                case OP_IDIV: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++], rb = bytecode[ip++];
                    if (!both_numeric(regs[ra], regs[rb])) { print_error("[Error] IDIV 需要数值\n"); return; }
                    BigInt ia = (regs[ra].type == Value::INT) ? regs[ra].big : BigInt::from_ll((long long)regs[ra].f);
                    BigInt ib = (regs[rb].type == Value::INT) ? regs[rb].big : BigInt::from_ll((long long)regs[rb].f);
                    if (ib.is_zero()) { 
                        if (!raise_interrupt(DIVZERO_INT)) { print_error("[Error] 除零\n"); return; } 
                        break; 
                    }
                    regs[rd] = Value(BigInt::div(ia, ib));
                    break;
                }
                case OP_POW: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++], rb = bytecode[ip++];
                    Value a = regs[ra], b = regs[rb];
                    if (!both_numeric(a, b)) { print_error("[Error] POW 需要数值\n"); return; }
                    if (a.type == Value::INT && b.type == Value::INT && !b.big.neg) {
                        regs[rd] = Value(BigInt::pow(a.big, b.big));
                    } else {
                        regs[rd] = Value(std::pow(to_double(a), to_double(b)), merge_prec(a, b));
                    }
                    break;
                }
                case OP_LESS: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++], rb = bytecode[ip++];
                    Value a = regs[ra], b = regs[rb];
                    if (a.type == Value::STRING && b.type == Value::STRING)
                        regs[rd] = Value(a.s < b.s ? 1 : 0);
                    else if (a.type == Value::INT && b.type == Value::INT)
                        regs[rd] = Value(BigInt::cmp(a.big, b.big) < 0 ? 1 : 0);
                    else
                        regs[rd] = Value(to_double(a) < to_double(b) ? 1 : 0);
                    break;
                }
                case OP_GREATER: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++], rb = bytecode[ip++];
                    Value a = regs[ra], b = regs[rb];
                    if (a.type == Value::STRING && b.type == Value::STRING)
                        regs[rd] = Value(a.s > b.s ? 1 : 0);
                    else if (a.type == Value::INT && b.type == Value::INT)
                        regs[rd] = Value(BigInt::cmp(a.big, b.big) > 0 ? 1 : 0);
                    else
                        regs[rd] = Value(to_double(a) > to_double(b) ? 1 : 0);
                    break;
                }
                case OP_EQUAL: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++], rb = bytecode[ip++];
                    Value a = regs[ra], b = regs[rb];
                    if (a.type == Value::STRING && b.type == Value::STRING)
                        regs[rd] = Value(a.s == b.s ? 1 : 0);
                    else if (a.type == Value::INT && b.type == Value::INT)
                        regs[rd] = Value(BigInt::cmp(a.big, b.big) == 0 ? 1 : 0);
                    else
                        regs[rd] = Value(to_double(a) == to_double(b) ? 1 : 0);
                    break;
                }
                case OP_OR: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++], rb = bytecode[ip++];
                    regs[rd] = Value((regs[ra].truthy() || regs[rb].truthy()) ? 1 : 0);
                    break;
                }
                case OP_AND: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++], rb = bytecode[ip++];
                    regs[rd] = Value((regs[ra].truthy() && regs[rb].truthy()) ? 1 : 0);
                    break;
                }
                case OP_NOT: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++];
                    regs[rd] = Value(regs[ra].truthy() ? 0 : 1);
                    break;
                }
                case OP_JMP: {
                    uint16_t target = bytecode[ip] | (bytecode[ip+1] << 8);
                    ip = target;
                    break;
                }
                case OP_JUMP_IF_FALSE: {
                    uint8_t ra = bytecode[ip++];
                    uint16_t target = bytecode[ip] | (bytecode[ip+1] << 8);
                    if (!regs[ra].truthy()) ip = target; else ip += 2;
                    break;
                }
                case OP_PRINT: {
                    uint8_t ra = bytecode[ip++];
                    std::string out_str = regs[ra].toString() + "\n";
                    if (io_fd != -1) {
                        size_t written = 0;
                        while (written < out_str.size()) {
                            ssize_t n = write(io_fd, out_str.c_str() + written, out_str.size() - written);
                            if (n <= 0) break;
                            written += n;
                        }
                    } else {
                        std::cout << out_str;
                    }
                    break;
                }
                case OP_PRINT_NO_NL: {
                    uint8_t ra = bytecode[ip++];
                    std::string out_str = regs[ra].toString();
                    if (io_fd != -1) {
                        size_t written = 0;
                        while (written < out_str.size()) {
                            ssize_t n = write(io_fd, out_str.c_str() + written, out_str.size() - written);
                            if (n <= 0) break;
                            written += n;
                        }
                    } else {
                        std::cout << out_str;
                    }
                    break;
                }
                case OP_INPUT: {
                    uint8_t ra = bytecode[ip++];
                    std::string line;
                    if (io_fd != -1) {
                        char buf[1024];
                        ssize_t n = read(io_fd, buf, sizeof(buf) - 1);
                        if (n > 0) {
                            buf[n] = '\0';
                            line = buf;
                            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
                        }
                    } else {
                        if (!std::getline(std::cin, line)) {
                            std::cin.clear();
                        }
                    }
                    regs[ra] = Value(line);
                    break;
                }
                case OP_HALT: {
                    if (!check_clean()) {
                        if (!raise_interrupt(DIRTY_INT)) {
                            print_error("[HEAP-VM] 物理堆未清空，强制退出\n");
                            return;
                        }
                        break;
                    }
                    return;
                }
                case OP_CALL: {
                    uint16_t target = bytecode[ip] | (bytecode[ip + 1] << 8); ip += 2;
                    call_stack.push_back(ip);
                    ip = target;
                    break;
                }
                case OP_RET: {
                    if (!call_stack.empty()) { ip = call_stack.back(); call_stack.pop_back(); }
                    else { return; }
                    break;
                }
                case OP_REG: {
                    uint8_t intno = bytecode[ip++];
                    uint16_t target = bytecode[ip] | (bytecode[ip + 1] << 8); ip += 2;
                    interrupts[intno] = (size_t)target;
                    break;
                }
                case OP_INT: {
                    uint8_t intno = bytecode[ip++];
                    if (!raise_interrupt(intno)) {
                        print_error("[HEAP-VM] 未注册中断\n");
                    }
                    break;
                }
                case OP_IRET: {
                    if (!call_stack.empty()) { ip = call_stack.back(); call_stack.pop_back(); }
                    break;
                }
                case OP_PUSH: case OP_POP: case OP_PUSH_F: case OP_PUSH_S:
                case OP_DEL: case OP_NOP: case OP_GOTO:
                    break;
                default:
                    print_error("[HEAP-VM] 未知指令: 0x" + std::to_string(op) + "\n");
                    return;
            }
        }
    }
};
#endif