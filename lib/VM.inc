#ifndef XY_VM_H
#define XY_VM_H

#include <iostream>
#include <vector>
#include <stack>
#include <string>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <unistd.h>

// ================= 指令集定义 =================
enum OpCode {
    OP_PUSH = 0x00, OP_POP = 0x01, OP_PRINT = 0x02, OP_ADD = 0x04,
    OP_SUB = 0x05, OP_MUL = 0x06, OP_DIV = 0x07, OP_JMP = 0x08,
    OP_LOAD = 0x0A, OP_STORE = 0x0B, OP_JUMP_IF_FALSE = 0x0C,
    OP_DEL = 0x0D, OP_NOP = 0x0E, OP_HALT = 0x0F,
    OP_LESS = 0x10, OP_GREATER = 0x11, OP_EQUAL = 0x12, OP_INPUT = 0x13,
    OP_OR = 0x14, OP_AND = 0x15,
    OP_GOTO = 0x16,
    OP_TRUE  = 0x17,
    OP_FALSE = 0x18,
    OP_REG = 0x1A,
    OP_PUSH_F = 0x1B,
    OP_PUSH_S = 0x1C,
    OP_INT = 0x1D,
    OP_IDIV = 0x1E,
    OP_POW = 0x1F,
    OP_NOT = 0x20,
    OP_CALL = 0x21,
    OP_RET = 0x22,
    OP_IRET = 0x23,
    OP_PRINT_NO_NL = 0x24 // 🚀 无换行打印
};

// ============================================================
// 🚀 BigInt —— 任意精度整数
// ============================================================
struct BigInt {
    bool        neg    = false;
    std::string digits = "0";

    BigInt() {}
    BigInt(int v) { *this = from_int(v); }

    static BigInt from_int(int v) {
        BigInt b;
        long long x = v;
        if (x < 0) { b.neg = true; x = -x; }
        if (x == 0) { b.digits = "0"; return b; }
        b.digits.clear();
        while (x) { b.digits += char('0' + x % 10); x /= 10; }
        std::reverse(b.digits.begin(), b.digits.end());
        return b;
    }
    static BigInt from_ll(long long x) {
        BigInt b;
        if (x < 0) { b.neg = true; x = -x; }
        if (x == 0) { b.digits = "0"; return b; }
        b.digits.clear();
        while (x) { b.digits += char('0' + x % 10); x /= 10; }
        std::reverse(b.digits.begin(), b.digits.end());
        return b;
    }
    static BigInt parse(const std::string& s) {
        BigInt b;
        size_t i = 0;
        while (i < s.size() && isspace((unsigned char)s[i])) i++;
        if (i < s.size() && s[i] == '-') { b.neg = true; i++; }
        else if (i < s.size() && s[i] == '+') i++;
        b.digits.clear();
        for (; i < s.size() && isdigit((unsigned char)s[i]); ++i) b.digits += s[i];
        if (b.digits.empty()) { b.digits = "0"; b.neg = false; return b; }
        size_t k = 0;
        while (k + 1 < b.digits.size() && b.digits[k] == '0') k++;
        b.digits = b.digits.substr(k);
        if (b.digits == "0") b.neg = false;
        return b;
    }

    std::string to_string() const {
        return (neg && digits != "0" ? "-" : "") + digits;
    }
    bool is_zero() const { return digits == "0"; }

    static int cmp_abs(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
        if (a < b) return -1;
        if (a > b) return  1;
        return 0;
    }
    static int cmp(const BigInt& a, const BigInt& b) {
        if (a.neg != b.neg) return a.neg ? -1 : 1;
        int c = cmp_abs(a.digits, b.digits);
        return a.neg ? -c : c;
    }

    static std::string add_abs(const std::string& a, const std::string& b) {
        std::string r;
        int i = (int)a.size() - 1, j = (int)b.size() - 1, carry = 0;
        while (i >= 0 || j >= 0 || carry) {
            int s = carry;
            if (i >= 0) s += a[i--] - '0';
            if (j >= 0) s += b[j--] - '0';
            r += char('0' + s % 10);
            carry = s / 10;
        }
        std::reverse(r.begin(), r.end());
        return r;
    }
    static std::string sub_abs(const std::string& a, const std::string& b) {
        std::string r;
        int i = (int)a.size() - 1, j = (int)b.size() - 1, borrow = 0;
        while (i >= 0) {
            int s = (a[i] - '0') - borrow - (j >= 0 ? b[j] - '0' : 0);
            if (s < 0) { s += 10; borrow = 1; } else borrow = 0;
            r += char('0' + s);
            i--; if (j >= 0) j--;
        }
        std::reverse(r.begin(), r.end());
        size_t k = 0;
        while (k + 1 < r.size() && r[k] == '0') k++;
        return r.substr(k);
    }
    static std::string mul_abs(const std::string& a, const std::string& b) {
        if (a == "0" || b == "0") return "0";
        std::vector<int> res(a.size() + b.size(), 0);
        for (int i = (int)a.size() - 1; i >= 0; --i) {
            for (int j = (int)b.size() - 1; j >= 0; --j) {
                int mul = (a[i] - '0') * (b[j] - '0');
                int p1 = i + j, p2 = i + j + 1;
                int sum = mul + res[p2];
                res[p2] = sum % 10;
                res[p1] += sum / 10;
            }
        }
        std::string r;
        for (int x : res) if (!(r.empty() && x == 0)) r += char('0' + x);
        return r.empty() ? "0" : r;
    }
    static std::string div_abs(const std::string& a, const std::string& b) {
        if (b == "0") return "0";
        if (cmp_abs(a, b) < 0) return "0";
        std::string q;
        std::string cur = "";
        for (char c : a) {
            cur += c;
            size_t k = 0;
            while (k + 1 < cur.size() && cur[k] == '0') k++;
            cur = cur.substr(k);
            if (cur.empty()) cur = "0";
            int d = 0;
            while (cmp_abs(cur, b) >= 0) { cur = sub_abs(cur, b); d++; }
            q += char('0' + d);
        }
        size_t k = 0;
        while (k + 1 < q.size() && q[k] == '0') k++;
        return q.empty() ? "0" : q.substr(k);
    }

    static BigInt add(const BigInt& a, const BigInt& b) {
        BigInt r;
        if (a.neg == b.neg) {
            r.digits = add_abs(a.digits, b.digits);
            r.neg    = a.neg;
        } else {
            int c = cmp_abs(a.digits, b.digits);
            if (c == 0)      { r.digits = "0"; r.neg = false; }
            else if (c > 0)  { r.digits = sub_abs(a.digits, b.digits); r.neg = a.neg; }
            else             { r.digits = sub_abs(b.digits, a.digits); r.neg = b.neg; }
        }
        if (r.digits == "0") r.neg = false;
        return r;
    }
    static BigInt sub(const BigInt& a, const BigInt& b) {
        BigInt nb = b;
        nb.neg = !b.neg;
        if (b.digits == "0") nb.neg = false;
        return add(a, nb);
    }
    static BigInt mul(const BigInt& a, const BigInt& b) {
        BigInt r;
        r.digits = mul_abs(a.digits, b.digits);
        r.neg    = (a.neg != b.neg) && r.digits != "0";
        return r;
    }
    static BigInt div(const BigInt& a, const BigInt& b) {
        BigInt r;
        if (b.digits == "0") return r;
        r.digits = div_abs(a.digits, b.digits);
        r.neg    = (a.neg != b.neg) && r.digits != "0";
        return r;
    }
    static BigInt pow(const BigInt& base, const BigInt& exp) {
        if (exp.neg) return BigInt::from_int(0);
        long long n = 0;
        try { n = std::stoll(exp.digits); }
        catch (...) { return BigInt::from_int(0); }
        if (n > 1000000) return BigInt::from_int(0);
        BigInt result = BigInt::from_int(1);
        BigInt b = base;
        while (n > 0) {
            if (n & 1) result = mul(result, b);
            b = mul(b, b);
            n >>= 1;
        }
        return result;
    }

    double to_double() const {
        try {
            double d = std::stod(digits);
            return neg ? -d : d;
        } catch (...) { return neg ? -1e308 : 1e308; }
    }
};

// ================= 指令结构体 =================
struct Instruction {
    OpCode opcode;
    int operand = 0;
    int operand2 = 0;
    double fval = 0.0;
    std::string sval;
    int fprec = -1;
};

// ================= 带类型的数据值 =================
struct Value {
    enum Type { INT, FLOAT, STRING, ADDR };

    Type     type;
    uint32_t addr = 0;
    BigInt   big;
    double   f = 0.0;
    std::string s;
    int      prec = -1;

    Value()                     : type(INT) {}
    Value(int v)                : type(INT),    big(BigInt::from_int(v)) {}
    Value(const BigInt& b)      : type(INT),    big(b) {}
    Value(double v, int p = -1) : type(FLOAT),  f(v), prec(p) {}
    Value(const std::string& v) : type(STRING), s(v)      {}
    Value(uint32_t a)           : type(ADDR),   addr(a)   {}

    static Value make_addr(uint32_t a) {
        Value v; v.type = ADDR; v.addr = a;
        return v;
    }

    static std::string format_float(double v, int prec) {
        if (prec < 0) prec = 6;
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(prec) << v;
        std::string s = oss.str();
        if (s.find('.') != std::string::npos) {
            size_t last = s.find_last_not_of('0');
            if (last != std::string::npos && s[last] == '.') last--;
            if (last != std::string::npos) s = s.substr(0, last + 1);
        }
        return s;
    }

    std::string toString() const {
        switch (type) {
            case INT:    return big.to_string();
            case FLOAT:  return format_float(f, prec);
            case STRING: return s;
            case ADDR: {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "0x%X", addr);
                return std::string(buf);
            }
        }
        return "";
    }

    bool truthy() const {
        switch (type) {
            case INT:    return !big.is_zero();
            case FLOAT:  return f != 0.0;
            case STRING: return !s.empty();
            case ADDR:   return addr != 0;
        }
        return false;
    }
};

// ================= 类型提升辅助 =================
static double to_double(const Value& v) {
    if (v.type == Value::INT)   return v.big.to_double();
    if (v.type == Value::FLOAT) return v.f;
    if (v.type == Value::ADDR)  return (double)v.addr;
    return 0.0;
}

static bool both_numeric(const Value& a, const Value& b) {
    return (a.type != Value::STRING) && (b.type != Value::STRING);
}

static int merge_prec(const Value& a, const Value& b) {
    int pa = (a.type == Value::FLOAT) ? a.prec : -1;
    int pb = (b.type == Value::FLOAT) ? b.prec : -1;
    if (pa < 0) return pb;
    if (pb < 0) return pa;
    return std::max(pa, pb);
}

// ================= 星缘虚拟机引擎 =================
class XingYuanVM {
private:
    std::stack<Value> dataStack;
    std::unordered_map<char, Value> variables;
    
    // 🚀 记录每个变量的类型，实现运行时的强类型锁定
    std::unordered_map<char, Value::Type> var_types;
    
    std::unordered_map<int, size_t> interrupts;
    std::vector<size_t> call_stack;
    size_t ip = 0;
    
    int io_fd = -1; 

    static constexpr size_t MAX_EXEC    = 100000000;
    static constexpr int    TIMEOUT_INT = 0;
    static constexpr int    DIVZERO_INT = 1;

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
    void run(const std::vector<Instruction>& bytecode, int fd = -1) {
        io_fd = fd;
        ip = 0;
        size_t exec_count = 0;

        while (ip < bytecode.size()) {
            if (++exec_count > MAX_EXEC) {
                if (raise_interrupt(TIMEOUT_INT)) {
                    exec_count = 0;
                    print_error("[VM] 执行超时，触发中断\n");
                    continue;
                } else {
                    print_error("[VM] 执行超时，无处理程序，强制退出\n");
                    return;
                }
            }

            Instruction inst = bytecode[ip++];

            switch (inst.opcode) {
                case OP_PUSH:
                    dataStack.push(Value(inst.operand));
                    break;

                case OP_PUSH_F:
                    dataStack.push(Value(inst.fval, inst.fprec));
                    break;

                case OP_PUSH_S:
                    dataStack.push(Value(inst.sval));
                    break;

                case OP_POP:
                    if (!dataStack.empty()) dataStack.pop();
                    break;

                case OP_PRINT:
                    if (!dataStack.empty()) {
                        std::string out_str = dataStack.top().toString() + "\n";
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
                        dataStack.pop();
                    }
                    break;

                // 🚀 新增：无换行打印
                case OP_PRINT_NO_NL:
                    if (!dataStack.empty()) {
                        std::string out_str = dataStack.top().toString(); 
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
                        dataStack.pop();
                    }
                    break;

                // 🚀 严格强类型 OP_ADD（修复栈失衡）
                case OP_ADD: {
                    if (dataStack.size() < 2) { print_error("[错误] 栈溢出\n"); return; }
                    Value b = dataStack.top(); dataStack.pop();
                    Value a = dataStack.top(); dataStack.pop();
                    
                    if (a.type == Value::STRING && b.type == Value::STRING) {
                        dataStack.push(Value(a.s + b.s));
                    } else if (a.type == Value::INT && b.type == Value::INT) {
                        dataStack.push(Value(BigInt::add(a.big, b.big)));
                    } else if (both_numeric(a, b)) {
                        dataStack.push(Value(to_double(a) + to_double(b), merge_prec(a, b)));
                    } else {
                        print_error("[Runtime Error] ADD 类型不匹配\n");
                        return; // 🚀 直接中断执行，防止后续控制流崩溃
                    }
                    break;
                }
                case OP_SUB: {
                    if (dataStack.size() < 2) { print_error("[错误] 栈溢出\n"); return; }
                    Value b = dataStack.top(); dataStack.pop();
                    Value a = dataStack.top(); dataStack.pop();
                    if (!both_numeric(a, b)) { 
                        print_error("[错误] SUB 需要数值\n");
                        return; // 🚀 中断执行
                    }
                    if (a.type == Value::INT && b.type == Value::INT)
                        dataStack.push(Value(BigInt::sub(a.big, b.big)));
                    else
                        dataStack.push(Value(to_double(a) - to_double(b), merge_prec(a, b)));
                    break;
                }
                case OP_MUL: {
                    if (dataStack.size() < 2) { print_error("[错误] 栈溢出\n"); return; }
                    Value b = dataStack.top(); dataStack.pop();
                    Value a = dataStack.top(); dataStack.pop();
                    if (!both_numeric(a, b)) { 
                        print_error("[错误] MUL 需要数值\n");
                        return; // 🚀 中断执行
                    }
                    if (a.type == Value::INT && b.type == Value::INT)
                        dataStack.push(Value(BigInt::mul(a.big, b.big)));
                    else
                        dataStack.push(Value(to_double(a) * to_double(b), merge_prec(a, b)));
                    break;
                }

                case OP_DIV: {
                    if (dataStack.size() < 2) { print_error("[错误] 栈溢出\n"); return; }
                    Value b = dataStack.top(); dataStack.pop();
                    Value a = dataStack.top(); dataStack.pop();
                    if (!both_numeric(a, b)) { 
                        print_error("[错误] DIV 需要数值\n");
                        return;
                    }
                    if (a.type == Value::INT && b.type == Value::INT) {
                        if (b.big.is_zero()) {
                            if (!raise_interrupt(DIVZERO_INT)) {
                                print_error("[错误] 除以零，无处理程序\n");
                                return;
                            }
                            break;
                        }
                        dataStack.push(Value(BigInt::div(a.big, b.big)));
                    } else {
                        double db = to_double(b);
                        if (db == 0.0) {
                            if (!raise_interrupt(DIVZERO_INT)) {
                                print_error("[错误] 除以零，无处理程序\n");
                                return;
                            }
                            break;
                        }
                        dataStack.push(Value(to_double(a) / db, merge_prec(a, b)));
                    }
                    break;
                }

                case OP_IDIV: {
                    if (dataStack.size() < 2) { print_error("[错误] 栈溢出\n"); return; }
                    Value b = dataStack.top(); dataStack.pop();
                    Value a = dataStack.top(); dataStack.pop();
                    if (!both_numeric(a, b)) { 
                        print_error("[错误] IDIV 需要数值\n");
                        return;
                    }
                    BigInt ia = (a.type == Value::INT) ? a.big : BigInt::from_ll((long long)a.f);
                    BigInt ib = (b.type == Value::INT) ? b.big : BigInt::from_ll((long long)b.f);
                    if (ib.is_zero()) {
                        if (!raise_interrupt(DIVZERO_INT)) {
                            print_error("[错误] 整除零，无处理程序\n");
                            return;
                        }
                        break;
                    }
                    dataStack.push(Value(BigInt::div(ia, ib)));
                    break;
                }

                case OP_POW: {
                    if (dataStack.size() < 2) { print_error("[错误] 栈溢出\n"); return; }
                    Value b = dataStack.top(); dataStack.pop();
                    Value a = dataStack.top(); dataStack.pop();
                    if (!both_numeric(a, b)) { 
                        print_error("[错误] POW 需要数值\n");
                        return;
                    }

                    if (a.type == Value::INT && b.type == Value::INT && !b.big.neg) {
                        dataStack.push(Value(BigInt::pow(a.big, b.big)));
                    } else {
                        dataStack.push(Value(std::pow(to_double(a), to_double(b)), merge_prec(a, b)));
                    }
                    break;
                }

                case OP_LOAD: {
                    char varName = (char)inst.operand;
                    auto it = variables.find(varName);
                    if (it != variables.end()) {
                        dataStack.push(it->second);
                    } else {
                        print_error("[错误] 变量 " + std::string(1, varName) + " 未定义！\n");
                        return; // 🚀 中断执行，防止缺失值导致控制流崩溃
                    }
                    break;
                }

                // 🚀 强类型锁定
                case OP_STORE: {
                    char varName = (char)inst.operand;
                    if (!dataStack.empty()) {
                        Value val = dataStack.top();
                        dataStack.pop();
                        
                        auto type_it = var_types.find(varName);
                        if (type_it == var_types.end()) {
                            var_types[varName] = val.type;
                        } else {
                            if (type_it->second != val.type) {
                                print_error("[Runtime Error] 类型不匹配！变量已声明为强类型，无法覆盖。\n");
                                return; // 🚀 中断执行
                            }
                        }
                        
                        variables[varName] = val;
                    } else {
                        print_error("[Runtime Error] 栈空，无法存储变量\n");
                        return;
                    }
                    break;
                }

                case OP_LESS: {
                    if (dataStack.size() < 2) { print_error("[错误] 栈溢出\n"); return; }
                    Value b = dataStack.top(); dataStack.pop();
                    Value a = dataStack.top(); dataStack.pop();
                    if (a.type == Value::STRING && b.type == Value::STRING)
                        dataStack.push(Value(a.s < b.s ? 1 : 0));
                    else if (a.type == Value::INT && b.type == Value::INT)
                        dataStack.push(Value(BigInt::cmp(a.big, b.big) < 0 ? 1 : 0));
                    else
                        dataStack.push(Value(to_double(a) < to_double(b) ? 1 : 0));
                    break;
                }
                case OP_GREATER: {
                    if (dataStack.size() < 2) { print_error("[错误] 栈溢出\n"); return; }
                    Value b = dataStack.top(); dataStack.pop();
                    Value a = dataStack.top(); dataStack.pop();
                    if (a.type == Value::STRING && b.type == Value::STRING)
                        dataStack.push(Value(a.s > b.s ? 1 : 0));
                    else if (a.type == Value::INT && b.type == Value::INT)
                        dataStack.push(Value(BigInt::cmp(a.big, b.big) > 0 ? 1 : 0));
                    else
                        dataStack.push(Value(to_double(a) > to_double(b) ? 1 : 0));
                    break;
                }
                case OP_EQUAL: {
                    if (dataStack.size() < 2) { print_error("[错误] 栈溢出\n"); return; }
                    Value b = dataStack.top(); dataStack.pop();
                    Value a = dataStack.top(); dataStack.pop();
                    int r = 0;
                    if (a.type == Value::STRING && b.type == Value::STRING)
                        r = (a.s == b.s) ? 1 : 0;
                    else if (a.type == Value::INT && b.type == Value::INT)
                        r = (BigInt::cmp(a.big, b.big) == 0) ? 1 : 0;
                    else if (both_numeric(a, b))
                        r = (to_double(a) == to_double(b)) ? 1 : 0;
                    dataStack.push(Value(r));
                    break;
                }

                case OP_OR: {
                    if (dataStack.size() < 2) { print_error("[错误] 栈溢出\n"); return; }
                    Value b = dataStack.top(); dataStack.pop();
                    Value a = dataStack.top(); dataStack.pop();
                    dataStack.push(Value((a.truthy() || b.truthy()) ? 1 : 0));
                    break;
                }
                case OP_AND: {
                    if (dataStack.size() < 2) { print_error("[错误] 栈溢出\n"); return; }
                    Value b = dataStack.top(); dataStack.pop();
                    Value a = dataStack.top(); dataStack.pop();
                    dataStack.push(Value((a.truthy() && b.truthy()) ? 1 : 0));
                    break;
                }
                case OP_NOT: {
                    if (dataStack.empty()) { print_error("[错误] 栈溢出\n"); return; }
                    Value a = dataStack.top(); dataStack.pop();
                    dataStack.push(Value(a.truthy() ? 0 : 1));
                    break;
                }

                case OP_REG:
                    interrupts[inst.operand] = (size_t)inst.operand2;
                    break;

                case OP_INT:
                    if (!raise_interrupt(inst.operand)) {
                        print_error("[VM] 未注册中断\n");
                    }
                    break;

                case OP_IRET:
                    if (!call_stack.empty()) {
                        ip = call_stack.back();
                        call_stack.pop_back();
                    }
                    break;

                case OP_CALL:
                    call_stack.push_back(ip);
                    ip = inst.operand;
                    break;

                case OP_RET: {
                    Value ret = dataStack.empty() ? Value(0) : dataStack.top();
                    if (!dataStack.empty()) dataStack.pop();
                    if (call_stack.empty()) {
                        return;
                    }
                    ip = call_stack.back();
                    call_stack.pop_back();
                    dataStack.push(ret);
                    break;
                }

                case OP_JMP:
                    ip = inst.operand;
                    break;

                case OP_GOTO:
                    ip = inst.operand;
                    break;

                case OP_TRUE:
                    dataStack.push(Value(1));
                    break;
                case OP_FALSE:
                    dataStack.push(Value(0));
                    break;

                case OP_JUMP_IF_FALSE: {
                    if (dataStack.empty()) { 
                        print_error("[Runtime Error] JUMP_IF_FALSE 栈空！控制流崩溃。\n"); 
                        return; // 🚀 中断执行
                    }
                    Value c = dataStack.top(); dataStack.pop();
                    if (!c.truthy()) ip = inst.operand;
                    break;
                }

                case OP_INPUT: {
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
                    dataStack.push(Value(line)); 
                    break;
                }

                case OP_DEL:
                    variables.erase((char)inst.operand);
                    var_types.erase((char)inst.operand);
                    break;

                case OP_NOP:
                    break;

                case OP_HALT:
                    return;

                default:
                    print_error("[虚拟机错误] 未知指令\n");
                    return;
            }
        }
    }
};

#endif