#ifndef XY_VM_REG_H
#define XY_VM_REG_H
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include "VM.inc"

enum RegExtraOpCode {
    OP_MOV_IMM = 0x40,
    OP_MOV = 0x41,
    OP_MOV_F = 0x42,
    OP_MOV_S = 0x43
};

class XingYuanRegVM {
private:
    static constexpr size_t REG_COUNT = 512;
    static constexpr size_t MAX_EXEC = 100000000;
    static constexpr int TIMEOUT_INT = 0;
    static constexpr int DIVZERO_INT = 1;

    std::vector<Value> regs;
    std::unordered_map<char, Value> variables;
    
    // 🚀 记录变量的强类型，防止重复定义不同类型
    std::unordered_map<char, Value::Type> var_types;

    std::unordered_map<int, size_t> interrupts;
    std::vector<size_t> call_stack;
    size_t ip = 0;
    int io_fd = -1;

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
    XingYuanRegVM() : regs(REG_COUNT) {}

    void run(const std::vector<uint8_t>& bytecode, int fd = -1) {
        io_fd = fd;
        ip = 0;
        size_t exec_count = 0;

        while (ip < bytecode.size()) {
            if (++exec_count > MAX_EXEC) {
                if (raise_interrupt(TIMEOUT_INT)) {
                    exec_count = 0;
                    print_error("[REG-VM] 执行超时，触发中断\n");
                    continue;
                } else {
                    print_error("\n[REG-VM] 执行超时，无处理程序，强制退出！\n");
                    return;
                }
            }

            uint8_t op = bytecode[ip++];
            switch (op) {
                case OP_MOV_IMM: {
                    uint8_t rd = bytecode[ip++];
                    uint8_t imm = bytecode[ip++];
                    regs[rd] = Value((int)imm);
                    break;
                }
                case OP_MOV: {
                    uint8_t rd = bytecode[ip++];
                    uint8_t ra = bytecode[ip++];
                    regs[rd] = regs[ra];
                    break;
                }
                case OP_MOV_F: {
                    uint8_t rd = bytecode[ip++];
                    double fval = 0.0;
                    std::memcpy(&fval, &bytecode[ip], 8);
                    ip += 8;
                    uint8_t prec = bytecode[ip++];
                    regs[rd] = Value(fval, (int)prec);
                    break;
                }
                case OP_MOV_S: {
                    uint8_t rd = bytecode[ip++];
                    uint16_t len = bytecode[ip] | (bytecode[ip + 1] << 8);
                    ip += 2;
                    std::string sval(bytecode.begin() + ip, bytecode.begin() + ip + len);
                    ip += len;
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
                
                // 🚀 核心修复：未定义变量不再默认给 0，直接熔断退出
                case OP_LOAD: {
                    uint8_t rd = bytecode[ip++];
                    char var = (char)bytecode[ip++];
                    if (variables.find(var) != variables.end()) {
                        regs[rd] = variables[var];
                    } else {
                        print_error("[Runtime Error] 尝试加载未定义的变量！\n");
                        return; 
                    }
                    break;
                }
                
                // 🚀 强类型锁定
                case OP_STORE: {
                    char var = (char)bytecode[ip++];
                    uint8_t ra = bytecode[ip++];
                    Value val = regs[ra];

                    auto it = var_types.find(var);
                    if (it == var_types.end()) {
                        var_types[var] = val.type;
                    } else {
                        if (it->second != val.type) {
                            print_error("[Runtime Error] 类型不匹配！变量已声明为强类型，无法覆盖。\n");
                            return; // 🚀 类型错误直接熔断
                        }
                    }

                    variables[var] = val;
                    break;
                }
                
                // 🚀 核心修复：所有算术逻辑加入类型不匹配熔断
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
                        print_error("[Runtime Error] ADD 类型不匹配: 不能用 String 和 Number 相加\n");
                        return; // 🚀 遇到脏数据立刻熔断
                    }
                    break;
                }
                case OP_SUB: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++], rb = bytecode[ip++];
                    Value a = regs[ra], b = regs[rb];
                    if (!both_numeric(a, b)) {
                        print_error("[Runtime Error] SUB 需要数值\n");
                        return; // 🚀 熔断
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
                        return; // 🚀 熔断
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
                        return; // 🚀 熔断
                    }
                    if (a.type == Value::INT && b.type == Value::INT) {
                        if (b.big.is_zero()) {
                            if (!raise_interrupt(DIVZERO_INT)) {
                                print_error("[REG-VM] 除零错误，无处理程序\n");
                                return;
                            }
                            break;
                        }
                        regs[rd] = Value(BigInt::div(a.big, b.big));
                    } else {
                        double db = to_double(b);
                        if (db == 0.0) {
                            if (!raise_interrupt(DIVZERO_INT)) {
                                print_error("[REG-VM] 除零错误，无处理程序\n");
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
                    BigInt ia = (regs[ra].type == Value::INT) ? regs[ra].big : BigInt::from_ll((long long)regs[ra].f);
                    BigInt ib = (regs[rb].type == Value::INT) ? regs[rb].big : BigInt::from_ll((long long)regs[rb].f);
                    if (ib.is_zero()) {
                        if (!raise_interrupt(DIVZERO_INT)) {
                            print_error("[REG-VM] 整除零错误，无处理程序\n");
                            return;
                        }
                        break;
                    }
                    regs[rd] = Value(BigInt::div(ia, ib));
                    break;
                }
                case OP_POW: {
                    uint8_t rd = bytecode[ip++], ra = bytecode[ip++], rb = bytecode[ip++];
                    Value a = regs[ra], b = regs[rb];
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
                    if (!regs[ra].truthy()) ip = target;
                    else ip += 2;
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
                case OP_HALT: {
                    return;
                }
                case OP_CALL: {
                    uint16_t target = bytecode[ip] | (bytecode[ip + 1] << 8);
                    ip += 2;
                    call_stack.push_back(ip);
                    ip = target;
                    break;
                }
                case OP_RET: {
                    if (!call_stack.empty()) {
                        ip = call_stack.back();
                        call_stack.pop_back();
                    } else {
                        return;
                    }
                    break;
                }
                case OP_REG: {
                    uint8_t intno = bytecode[ip++];
                    uint16_t target = bytecode[ip] | (bytecode[ip + 1] << 8);
                    ip += 2;
                    interrupts[intno] = (size_t)target;
                    break;
                }
                case OP_INT: {
                    uint8_t intno = bytecode[ip++];
                    if (!raise_interrupt(intno)) {
                        print_error("[REG-VM] 未注册中断\n");
                    }
                    break;
                }
                case OP_IRET: {
                    if (!call_stack.empty()) {
                        ip = call_stack.back();
                        call_stack.pop_back();
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
                case OP_PUSH: case OP_POP: case OP_PUSH_F: case OP_PUSH_S:
                case OP_DEL: case OP_NOP:
                case OP_GOTO:
                    break;
                default:
                    print_error("[REG-VM] 未知或不支持的指令: 0x" + std::to_string(op) + "\n");
                    return;
            }
        }
    }
};
#endif