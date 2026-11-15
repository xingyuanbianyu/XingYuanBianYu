// ============================================================
// lib2.inc —— AST 字符串 → 堆式字节码
// 堆式专属编码段：0x50 ~ 0x57（与寄存器式的 0x40~0x43 完全隔离）
// 复用栈式通用 OpCode（OP_ADD/OP_PRINT/OP_JMP/...）
// ============================================================

// ============================================================
// 🚀 堆式专属指令（0x50 ~ 0x57）
// ============================================================
enum HeapCompilerOp {
    OP_ALLOC     = 0x50,
    OP_REF       = 0x51,
    OP_FREE      = 0x52,
    OP_DEREF     = 0x53,
    OP_H_MOV_IMM = 0x54,
    OP_H_MOV     = 0x55,
    OP_H_MOV_F   = 0x56,
    OP_H_MOV_S   = 0x57
};

// ============================================================
// 寄存器分配器
// ============================================================
struct HeapRegAllocator {
    std::map<char, int> var_reg_map;
    int next_reg = 0;

    int get_var_reg(char var) {
        auto it = var_reg_map.find(var);
        if (it == var_reg_map.end()) {
            var_reg_map[var] = next_reg++;
            return var_reg_map[var];
        }
        return it->second;
    }

    int alloc_temp() { return next_reg++; }
};

// ============================================================
// 前向声明
// ============================================================
static bool heap_emitOperand(const std::string& part,
                             HeapRegAllocator& ra,
                             std::vector<uint8_t>& bytes,
                             int& out_reg);

static bool heap_emitCondition(const std::string& raw_cond,
                               HeapRegAllocator& ra,
                               std::vector<uint8_t>& bytes,
                               int& out_reg);

// ============================================================
// 🚀 变量读取：LOAD 地址 → 取址拿值
// ============================================================
static void heap_emit_var_read(char var,
                               HeapRegAllocator& ra,
                               std::vector<uint8_t>& bytes,
                               int& out_reg) {
    int r_addr = ra.alloc_temp();
    bytes.push_back(static_cast<uint8_t>(OP_LOAD));
    bytes.push_back((uint8_t)r_addr);
    bytes.push_back((uint8_t)var);

    int r_val = ra.alloc_temp();
    bytes.push_back(static_cast<uint8_t>(OP_DEREF));
    bytes.push_back((uint8_t)r_val);
    bytes.push_back((uint8_t)r_addr);

    out_reg = r_val;
}

// ============================================================
// 🚀 变量写入：分址新 slot → STORE 地址到变量表
// ============================================================
static void heap_emit_var_write(char var, int src_reg,
                                HeapRegAllocator& ra,
                                std::vector<uint8_t>& bytes) {
    int r_addr = ra.alloc_temp();
    bytes.push_back(static_cast<uint8_t>(OP_ALLOC));
    bytes.push_back((uint8_t)r_addr);
    bytes.push_back((uint8_t)src_reg);

    bytes.push_back(static_cast<uint8_t>(OP_STORE));
    bytes.push_back((uint8_t)var);
    bytes.push_back((uint8_t)r_addr);
}

// ============================================================
// 操作数编译
// ============================================================
static bool heap_emitOperand(const std::string& part,
                             HeapRegAllocator& ra,
                             std::vector<uint8_t>& bytes,
                             int& out_reg) {
    // ============================================================
    // 🚀 {G ...} 分组：剥掉分组壳，递归处理内部
    // ============================================================
    {
        std::string s = trim(part);
        if (s.size() >= 4 && s.compare(0, 3, "{G ") == 0 && s.back() == '}') {
            int d = 0;
            bool whole_group = true;
            for (size_t i = 0; i < s.size(); ++i) {
                if (s[i] == '{') d++;
                else if (s[i] == '}') {
                    d--;
                    if (d == 0 && i != s.size() - 1) { whole_group = false; break; }
                }
            }
            if (whole_group) {
                std::string inner = trim(s.substr(3, s.size() - 4));
                return heap_emitOperand(inner, ra, bytes, out_reg);
            }
        }
    }

    std::string s = trim(part);
    if (s.empty()) return false;

    // 括号剥离
    while (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
        s = trim(s.substr(1, s.size() - 2));
    }

    // ============================================================
    // 🚀 【新增拦截】内置输入指令（必须在所有匹配之前拦截）
    // ============================================================
    if (s.find("FID:INPUT") != std::string::npos) {
        // 1. 提取提示词
        std::string prompt = "";
        size_t prompt_pos = s.find("SID:22[");
        if (prompt_pos != std::string::npos) {
            size_t start = prompt_pos + 7;
            size_t end = s.find("]22", start);
            if (end != std::string::npos) {
                prompt = s.substr(start, end - start);
            }
        }

        // 2. 如果有提示词，分配临时寄存器并打印
        if (!prompt.empty()) {
            int r_prompt = ra.alloc_temp();
            
            // 生成 H_MOV_S r_prompt, prompt
            bytes.push_back(OP_H_MOV_S);
            bytes.push_back((uint8_t)r_prompt);
            uint16_t len = (uint16_t)prompt.size();
            bytes.push_back((uint8_t)(len & 0xFF));
            bytes.push_back((uint8_t)((len >> 8) & 0xFF));
            for (char c : prompt) bytes.push_back((uint8_t)c);

            // 生成 PRINT_NO_NL r_prompt
            bytes.push_back(OP_PRINT_NO_NL); 
            bytes.push_back((uint8_t)r_prompt);
        }

        // 3. 分配一个寄存器接收输入结果
        int r_input = ra.alloc_temp();
        
        // 生成 INPUT r_input
        bytes.push_back(OP_INPUT);
        bytes.push_back((uint8_t)r_input);

        out_reg = r_input;
        return true;
    }
    // ============================================================

    // 🚀 函数调用
    if (s.find("FID:CALL") != std::string::npos) {
        size_t ns = s.find("NAME:SID:22[");
        if (ns == std::string::npos) return false;
        ns += 12;
        size_t ne = s.find("]22", ns);
        if (ne == std::string::npos) return false;
        std::string func_name = s.substr(ns, ne - ns);

        std::vector<std::string> arg_list;
        size_t args_pos = s.find("ARGS:");
        if (args_pos != std::string::npos) {
            args_pos += 5;
            std::string args = s.substr(args_pos);
            while (!args.empty() && args.back() == ' ') args.pop_back();
            std::string tmp;
            int depth = 0;
            for (char c : args) {
                if (c == '(') depth++;
                else if (c == ')') depth--;
                if (c == ',' && depth == 0) {
                    if (!tmp.empty()) { arg_list.push_back(trim(tmp)); tmp.clear(); }
                } else tmp += c;
            }
            if (!tmp.empty()) arg_list.push_back(trim(tmp));
        }

        for (size_t k = 0; k < arg_list.size(); ++k) {
            int r_arg = -1;
            heap_emitOperand(arg_list[k], ra, bytes, r_arg);
            bytes.push_back(static_cast<uint8_t>(OP_H_MOV));
            bytes.push_back((uint8_t)(k + 1));
            bytes.push_back((uint8_t)r_arg);
        }

        bytes.push_back(static_cast<uint8_t>(OP_CALL));
        size_t patch_pos = bytes.size();
        bytes.push_back(0x00); bytes.push_back(0x00);
        g_call_sites.push_back({func_name, patch_pos});

        out_reg = 0;
        return true;
    }

    // 二元运算
    std::string op_tag;
    OpCode op_code = OP_NOP;
    size_t op_pos = find_last_bin_op(s, op_tag, op_code);
    if (op_pos != std::string::npos) {
        std::string left  = s.substr(0, op_pos);
        std::string right = s.substr(op_pos + op_tag.length());

        int ra_reg = -1, rb_reg = -1;
        heap_emitOperand(left,  ra, bytes, ra_reg);
        heap_emitOperand(right, ra, bytes, rb_reg);

        int rd = ra.alloc_temp();
        bytes.push_back(static_cast<uint8_t>(op_code));
        bytes.push_back((uint8_t)rd);
        bytes.push_back((uint8_t)ra_reg);
        bytes.push_back((uint8_t)rb_reg);
        out_reg = rd;
        return true;
    }

    // 一元负号
    if (s.rfind("[SUB]", 0) == 0) {
        std::string inner = trim(s.substr(5));
        int inner_reg = -1;
        heap_emitOperand(inner, ra, bytes, inner_reg);
        int r_zero = ra.alloc_temp();
        bytes.push_back(static_cast<uint8_t>(OP_H_MOV_IMM));
        bytes.push_back((uint8_t)r_zero); bytes.push_back(0);
        int rd = ra.alloc_temp();
        bytes.push_back(static_cast<uint8_t>(OP_SUB));
        bytes.push_back((uint8_t)rd); bytes.push_back((uint8_t)r_zero); bytes.push_back((uint8_t)inner_reg);
        out_reg = rd;
        return true;
    }

    // 🚀 变量引用：走堆读
    size_t vid_pos = s.find("VID:");
    if (vid_pos != std::string::npos) {
        size_t start = vid_pos + 4;
        if (start < s.size()) {
            heap_emit_var_read(s[start], ra, bytes, out_reg);
            return true;
        }
    }

    // 整数
    size_t nid_pos = s.find("NID:");
    if (nid_pos != std::string::npos) {
        size_t start = nid_pos + 4;
        size_t end = start;
        if (start < s.size() && s[start] == '-') start++;
        while (end < s.size() && isdigit((unsigned char)s[end])) end++;
        if (end > start) {
            int val = std::stoi(s.substr(nid_pos + 4, end - (nid_pos + 4)));
            int rd = ra.alloc_temp();
            bytes.push_back(static_cast<uint8_t>(OP_H_MOV_IMM));
            bytes.push_back((uint8_t)rd);
            bytes.push_back((uint8_t)val);
            out_reg = rd;
            return true;
        }
    }

    // 浮点数
    size_t flt_pos = s.find("FLT:");
    if (flt_pos != std::string::npos) {
        size_t start = flt_pos + 4;
        size_t end = start;
        while (end < s.size() &&
               (isdigit((unsigned char)s[end]) || s[end] == '.' ||
                s[end] == '-' || s[end] == '+' ||
                s[end] == 'e' || s[end] == 'E')) end++;
        if (end > start) {
            std::string numstr = s.substr(start, end - start);
            double d = std::stod(numstr);
            int prec = 0;
            size_t dot = numstr.find('.');
            if (dot != std::string::npos) {
                for (size_t k = dot + 1; k < numstr.size(); ++k) {
                    if (isdigit((unsigned char)numstr[k])) prec++;
                    else break;
                }
            }
            int rd = ra.alloc_temp();
            bytes.push_back(static_cast<uint8_t>(OP_H_MOV_F));
            bytes.push_back((uint8_t)rd);
            uint64_t bits = 0;
            std::memcpy(&bits, &d, 8);
            for (int k = 0; k < 8; ++k)
                bytes.push_back((uint8_t)((bits >> (8*k)) & 0xFF));
            bytes.push_back((uint8_t)(prec & 0xFF));
            out_reg = rd; return true;
        }
    }

    // 字符串
    size_t sid_pos = s.find("SID:22[");
    if (sid_pos != std::string::npos) {
        size_t start = sid_pos + 7;
        size_t end = s.find("]22", start);
        if (end == std::string::npos) end = s.size();
        std::string content = s.substr(start, end - start);
        if (content.size() <= 65535) {
            int rd = ra.alloc_temp();
            bytes.push_back(static_cast<uint8_t>(OP_H_MOV_S));
            bytes.push_back((uint8_t)rd);
            uint16_t len = (uint16_t)content.size();
            bytes.push_back((uint8_t)(len & 0xFF));
            bytes.push_back((uint8_t)((len >> 8) & 0xFF));
            for (char c : content) bytes.push_back((uint8_t)c);
            out_reg = rd; return true;
        }
    }

    // 兜底
    int rd = ra.alloc_temp();
    bytes.push_back(static_cast<uint8_t>(OP_H_MOV_IMM));
    bytes.push_back((uint8_t)rd); bytes.push_back(0);
    out_reg = rd;
    return true;
}

// ============================================================
// 条件编译
// ============================================================
static bool heap_emitCondition(const std::string& raw_cond,
                               HeapRegAllocator& ra,
                               std::vector<uint8_t>& bytes,
                               int& out_reg) {
    std::string cond = trim(raw_cond);
    if (cond.empty()) return false;

    // 🚀 真/假字面量（仅用于条件判断）
    if (cond.find("BOOL:TRUE") != std::string::npos ||
        cond == "True" || cond == "true") {
        int rd = ra.alloc_temp();
        bytes.push_back(0x17);
        bytes.push_back((uint8_t)rd);
        out_reg = rd;
        return true;
    }
    if (cond.find("BOOL:FALSE") != std::string::npos ||
        cond == "False" || cond == "false") {
        int rd = ra.alloc_temp();
        bytes.push_back(0x18);
        bytes.push_back((uint8_t)rd);
        out_reg = rd;
        return true;
    }

    size_t or_pos = cond.rfind("[OR]");
    if (or_pos != std::string::npos) {
        int ra_reg, rb_reg;
        heap_emitCondition(cond.substr(0, or_pos), ra, bytes, ra_reg);
        heap_emitCondition(cond.substr(or_pos + 4), ra, bytes, rb_reg);
        int rd = ra.alloc_temp();
        bytes.push_back(static_cast<uint8_t>(OP_OR));
        bytes.push_back((uint8_t)rd); bytes.push_back((uint8_t)ra_reg); bytes.push_back((uint8_t)rb_reg);
        out_reg = rd; return true;
    }
    size_t and_pos = cond.rfind("[AND]");
    if (and_pos != std::string::npos) {
        int ra_reg, rb_reg;
        heap_emitCondition(cond.substr(0, and_pos), ra, bytes, ra_reg);
        heap_emitCondition(cond.substr(and_pos + 5), ra, bytes, rb_reg);
        int rd = ra.alloc_temp();
        bytes.push_back(static_cast<uint8_t>(OP_AND));
        bytes.push_back((uint8_t)rd); bytes.push_back((uint8_t)ra_reg); bytes.push_back((uint8_t)rb_reg);
        out_reg = rd; return true;
    }

    std::string op_tag;
    size_t op_pos = 0;
    if      ((op_pos = cond.find("[EQ]")) != std::string::npos) op_tag = "[EQ]";
    else if ((op_pos = cond.find("[LT]")) != std::string::npos) op_tag = "[LT]";
    else if ((op_pos = cond.find("[GT]")) != std::string::npos) op_tag = "[GT]";
    else if ((op_pos = cond.find("[NE]")) != std::string::npos) op_tag = "[NE]";
    else if ((op_pos = cond.find("[GE]")) != std::string::npos) op_tag = "[GE]";
    else if ((op_pos = cond.find("[LE]")) != std::string::npos) op_tag = "[LE]";

    if (!op_tag.empty()) {
        std::string left  = cond.substr(0, op_pos);
        std::string right = cond.substr(op_pos + op_tag.length());
        int ra_reg, rb_reg;
        heap_emitOperand(left,  ra, bytes, ra_reg);
        heap_emitOperand(right, ra, bytes, rb_reg);

        if (op_tag == "[EQ]") {
            int rd = ra.alloc_temp();
            bytes.push_back(OP_EQUAL); bytes.push_back((uint8_t)rd); bytes.push_back((uint8_t)ra_reg); bytes.push_back((uint8_t)rb_reg);
            out_reg = rd;
        } else if (op_tag == "[LT]") {
            int rd = ra.alloc_temp();
            bytes.push_back(OP_LESS); bytes.push_back((uint8_t)rd); bytes.push_back((uint8_t)ra_reg); bytes.push_back((uint8_t)rb_reg);
            out_reg = rd;
        } else if (op_tag == "[GT]") {
            int rd = ra.alloc_temp();
            bytes.push_back(OP_GREATER); bytes.push_back((uint8_t)rd); bytes.push_back((uint8_t)ra_reg); bytes.push_back((uint8_t)rb_reg);
            out_reg = rd;
        } else if (op_tag == "[NE]") {
            int rt = ra.alloc_temp();
            bytes.push_back(OP_EQUAL); bytes.push_back((uint8_t)rt); bytes.push_back((uint8_t)ra_reg); bytes.push_back((uint8_t)rb_reg);
            int rd = ra.alloc_temp();
            bytes.push_back(OP_NOT); bytes.push_back((uint8_t)rd); bytes.push_back((uint8_t)rt);
            out_reg = rd;
        } else if (op_tag == "[GE]") {
            int rt = ra.alloc_temp();
            bytes.push_back(OP_LESS); bytes.push_back((uint8_t)rt); bytes.push_back((uint8_t)ra_reg); bytes.push_back((uint8_t)rb_reg);
            int rd = ra.alloc_temp();
            bytes.push_back(OP_NOT); bytes.push_back((uint8_t)rd); bytes.push_back((uint8_t)rt);
            out_reg = rd;
        } else if (op_tag == "[LE]") {
            int rt = ra.alloc_temp();
            bytes.push_back(OP_GREATER); bytes.push_back((uint8_t)rt); bytes.push_back((uint8_t)ra_reg); bytes.push_back((uint8_t)rb_reg);
            int rd = ra.alloc_temp();
            bytes.push_back(OP_NOT); bytes.push_back((uint8_t)rd); bytes.push_back((uint8_t)rt);
            out_reg = rd;
        }
        return true;
    }

    return heap_emitOperand(cond, ra, bytes, out_reg);
}

// ============================================================
// 堆式主入口
// ============================================================
std::vector<uint8_t> compileFromASTStringHeap(const std::string& ast) {
    std::vector<uint8_t> bytes;
    HeapRegAllocator ra;

    // ============================================================
    // 🚀 自动注入异常处理程序 (HEAP-VM 版)
    // ============================================================
    // 预分配一个安全寄存器给错误处理程序使用（避开用户的变量）
    int err_reg = ra.alloc_temp();

    bytes.push_back(static_cast<uint8_t>(OP_JMP));
    size_t main_jmp_patch = bytes.size();
    // HEAP-VM 统一使用 2 字节跳转地址
    bytes.push_back(0x00); bytes.push_back(0x00);

    size_t handler_ip = bytes.size(); // 异常处理块起始地址

    std::string err_msg = "系统异常中断";
    // HEAP-VM 加载字符串：H_MOV_S Rd, len, chars
    bytes.push_back(static_cast<uint8_t>(OP_H_MOV_S));
    bytes.push_back((uint8_t)err_reg);
    uint16_t err_len = (uint16_t)err_msg.size();
    bytes.push_back((uint8_t)(err_len & 0xFF));
    bytes.push_back((uint8_t)((err_len >> 8) & 0xFF));
    for (char c : err_msg) bytes.push_back((uint8_t)c);

    bytes.push_back(static_cast<uint8_t>(OP_PRINT));
    bytes.push_back((uint8_t)err_reg);

    bytes.push_back(static_cast<uint8_t>(OP_HALT));

    // 回填主程序入口
    size_t main_ip = bytes.size();
    bytes[main_jmp_patch]     = (uint8_t)(main_ip & 0xFF);
    bytes[main_jmp_patch + 1] = (uint8_t)((main_ip >> 8) & 0xFF);

    // 注册中断 0（超时）和 1（除零）到异常处理块
    bytes.push_back(static_cast<uint8_t>(OP_REG));
    bytes.push_back(0);
    bytes.push_back((uint8_t)(handler_ip & 0xFF));
    bytes.push_back((uint8_t)((handler_ip >> 8) & 0xFF));

    bytes.push_back(static_cast<uint8_t>(OP_REG));
    bytes.push_back(1);
    bytes.push_back((uint8_t)(handler_ip & 0xFF));
    bytes.push_back((uint8_t)((handler_ip >> 8) & 0xFF));
    // ============================================================

    struct HeapBlockCtx {
        int type = 0;
        size_t if_patch = 0;
        size_t loop_start = 0;
        int    cond_reg = 0;
        char   loop_var = 0;
        int    end_val  = 0;
        std::vector<size_t> end_jumps;
        std::vector<size_t> break_patches;
        std::vector<size_t> continue_patches;
    };
    std::vector<HeapBlockCtx> stack;

    std::stringstream ss(ast);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(ss, line)) { if (!line.empty()) lines.push_back(line); }

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string cur = lines[i];
        std::string trimmed = trim(cur);

        if (trimmed.empty() || trimmed == "{" || trimmed == "}") {
            if (trimmed == "}") {
                bool next_branch = false;
                if (i + 1 < lines.size()) {
                    if (lines[i+1].find("KID:ELSE") != std::string::npos ||
                        lines[i+1].find("KID:ELIF") != std::string::npos) next_branch = true;
                }
                if (next_branch) continue;

                if (!stack.empty()) {
                    HeapBlockCtx ctx = stack.back();

                    if (ctx.type == 4) {
                        stack.pop_back();
                        bytes.push_back(static_cast<uint8_t>(OP_H_MOV_IMM));
                        bytes.push_back(0x00); bytes.push_back(0);
                        bytes.push_back(static_cast<uint8_t>(OP_RET));
                        bytes[ctx.if_patch]     = (uint8_t)(bytes.size() & 0xFF);
                        bytes[ctx.if_patch + 1] = (uint8_t)((bytes.size() >> 8) & 0xFF);
                        continue;
                    }

                    if (ctx.type == 0 || ctx.type == 1) {
                        stack.pop_back();
                        size_t end = bytes.size();
                        for (size_t p : ctx.end_jumps) {
                            bytes[p]     = (uint8_t)(end & 0xFF);
                            bytes[p + 1] = (uint8_t)((end >> 8) & 0xFF);
                        }
                        bytes[ctx.if_patch]     = (uint8_t)(end & 0xFF);
                        bytes[ctx.if_patch + 1] = (uint8_t)((end >> 8) & 0xFF);
                    }
                }
            }
            continue;
        }

        // ---------- 函数定义 ----------
        if (cur.find("DEF NAME:SID:") != std::string::npos) {
            size_t ns = cur.find("NAME:SID:22[");
            if (ns == std::string::npos) continue;
            ns += 12;
            size_t ne = cur.find("]22", ns);
            if (ne == std::string::npos) continue;
            std::string func_name = cur.substr(ns, ne - ns);

            std::vector<char> params;
            size_t p_start = cur.find("PARAMS:");
            if (p_start != std::string::npos) {
                p_start += 7;
                size_t p_end = cur.find("}", p_start);
                if (p_end == std::string::npos) p_end = cur.size();
                std::string p_str = cur.substr(p_start, p_end - p_start);
                std::stringstream pss(p_str);
                std::string tok;
                while (std::getline(pss, tok, ',')) {
                    tok = trim(tok);
                    size_t vp = tok.find("VID:");
                    if (vp != std::string::npos && vp + 4 < tok.size())
                        params.push_back(tok[vp + 4]);
                }
            }

            bytes.push_back(static_cast<uint8_t>(OP_JMP));
            size_t skip_patch = bytes.size();
            bytes.push_back(0x00); bytes.push_back(0x00);

            g_func_table[func_name] = bytes.size();

            for (size_t k = 0; k < params.size(); ++k) {
                int r_addr = ra.alloc_temp();
                bytes.push_back(static_cast<uint8_t>(OP_ALLOC));
                bytes.push_back((uint8_t)r_addr);
                bytes.push_back((uint8_t)(k + 1));
                bytes.push_back(static_cast<uint8_t>(OP_STORE));
                bytes.push_back((uint8_t)params[k]);
                bytes.push_back((uint8_t)r_addr);
            }

            HeapBlockCtx ctx;
            ctx.type = 4;
            ctx.if_patch = skip_patch;
            stack.push_back(ctx);
            continue;
        }

        // ---------- RETURN ----------
        if (cur.find("KID:RETURN") != std::string::npos) {
            size_t rs = cur.find("KID:RETURN") + 10;
            size_t re = cur.find("}", rs);
            if (re == std::string::npos) re = cur.size();
            std::string expr = trim(cur.substr(rs, re - rs));

            if (!expr.empty()) {
                int r_val = -1;
                heap_emitOperand(expr, ra, bytes, r_val);
                if (r_val != 0) {
                    bytes.push_back(static_cast<uint8_t>(OP_H_MOV));
                    bytes.push_back(0x00);
                    bytes.push_back((uint8_t)r_val);
                }
            } else {
                bytes.push_back(static_cast<uint8_t>(OP_H_MOV_IMM));
                bytes.push_back(0x00); bytes.push_back(0);
            }
            bytes.push_back(static_cast<uint8_t>(OP_RET));
            continue;
        }

        // ---------- FOR ----------
        if (cur.find("KID:FOR") != std::string::npos) {
            size_t name_pos = cur.find("NAME:VID:");
            if (name_pos == std::string::npos) continue;
            char loop_var = cur[name_pos + 9];

            size_t start_pos = cur.find("START:NID:");
            if (start_pos == std::string::npos) continue;
            start_pos += 10;
            size_t start_end = start_pos;
            while (start_end < cur.size() && isdigit((unsigned char)cur[start_end])) start_end++;
            int start_val = std::stoi(cur.substr(start_pos, start_end - start_pos));

            size_t end_pos = cur.find("END:NID:");
            if (end_pos == std::string::npos) continue;
            end_pos += 8;
            size_t end_end = end_pos;
            while (end_end < cur.size() && isdigit((unsigned char)cur[end_end])) end_end++;
            int end_val = std::stoi(cur.substr(end_pos, end_end - end_pos));

            {
                int r_init = ra.alloc_temp();
                bytes.push_back(static_cast<uint8_t>(OP_H_MOV_IMM));
                bytes.push_back((uint8_t)r_init);
                bytes.push_back((uint8_t)(start_val & 0xFF));
                heap_emit_var_write(loop_var, r_init, ra, bytes);
            }

            HeapBlockCtx ctx;
            ctx.type = 3;
            ctx.loop_var = loop_var;
            ctx.end_val  = end_val;
            ctx.loop_start = bytes.size();

            int r_i_val = -1;
            heap_emit_var_read(loop_var, ra, bytes, r_i_val);

            int r_end = ra.alloc_temp();
            bytes.push_back(static_cast<uint8_t>(OP_H_MOV_IMM));
            bytes.push_back((uint8_t)r_end);
            bytes.push_back((uint8_t)(end_val & 0xFF));

            int r_cond = ra.alloc_temp();
            bytes.push_back(static_cast<uint8_t>(OP_LESS));
            bytes.push_back((uint8_t)r_cond);
            bytes.push_back((uint8_t)r_i_val);
            bytes.push_back((uint8_t)r_end);

            bytes.push_back(static_cast<uint8_t>(OP_JUMP_IF_FALSE));
            bytes.push_back((uint8_t)r_cond);
            ctx.if_patch = bytes.size();
            bytes.push_back(0x00); bytes.push_back(0x00);

            stack.push_back(ctx);
            continue;
        }

        // ---------- ENDFOR ----------
        if (cur.find("KID:ENDFOR") != std::string::npos) {
            if (stack.empty() || stack.back().type != 3) continue;
            HeapBlockCtx ctx = stack.back(); stack.pop_back();

            for (size_t p : ctx.continue_patches) {
                bytes[p]     = (uint8_t)(ctx.loop_start & 0xFF);
                bytes[p + 1] = (uint8_t)((ctx.loop_start >> 8) & 0xFF);
            }

            {
                int r_i_val = -1;
                heap_emit_var_read(ctx.loop_var, ra, bytes, r_i_val);

                int r_one = ra.alloc_temp();
                bytes.push_back(static_cast<uint8_t>(OP_H_MOV_IMM));
                bytes.push_back((uint8_t)r_one);
                bytes.push_back(1);

                int r_new = ra.alloc_temp();
                bytes.push_back(static_cast<uint8_t>(OP_ADD));
                bytes.push_back((uint8_t)r_new);
                bytes.push_back((uint8_t)r_i_val);
                bytes.push_back((uint8_t)r_one);

                heap_emit_var_write(ctx.loop_var, r_new, ra, bytes);
            }

            bytes.push_back(static_cast<uint8_t>(OP_JMP));
            bytes.push_back((uint8_t)(ctx.loop_start & 0xFF));
            bytes.push_back((uint8_t)((ctx.loop_start >> 8) & 0xFF));

            size_t end = bytes.size();
            bytes[ctx.if_patch]     = (uint8_t)(end & 0xFF);
            bytes[ctx.if_patch + 1] = (uint8_t)((end >> 8) & 0xFF);
            for (size_t p : ctx.break_patches) {
                bytes[p]     = (uint8_t)(end & 0xFF);
                bytes[p + 1] = (uint8_t)((end >> 8) & 0xFF);
            }
            continue;
        }

        // ---------- WHILE ----------
        if (cur.find("KID:WHILE") != std::string::npos) {
            HeapBlockCtx ctx; ctx.type = 2; ctx.loop_start = bytes.size();
            size_t cp = cur.find("COND:");
            std::string cond_str;
            if (cp != std::string::npos) {
                cp += 5;
                size_t ce = cur.find("}", cp);
                cond_str = cur.substr(cp, ce == std::string::npos ? std::string::npos : ce - cp);
            }
            int cond_reg = -1;
            heap_emitCondition(cond_str, ra, bytes, cond_reg);
            ctx.cond_reg = cond_reg;

            bytes.push_back(static_cast<uint8_t>(OP_JUMP_IF_FALSE));
            bytes.push_back((uint8_t)cond_reg);
            ctx.if_patch = bytes.size();
            bytes.push_back(0x00); bytes.push_back(0x00);
            stack.push_back(ctx);
            continue;
        }

        // ---------- ENDWHILE ----------
        if (cur.find("KID:ENDWHILE") != std::string::npos) {
            if (stack.empty() || stack.back().type != 2) continue;
            HeapBlockCtx ctx = stack.back(); stack.pop_back();
            for (size_t p : ctx.continue_patches) {
                bytes[p]     = (uint8_t)(ctx.loop_start & 0xFF);
                bytes[p + 1] = (uint8_t)((ctx.loop_start >> 8) & 0xFF);
            }
            bytes.push_back(static_cast<uint8_t>(OP_JMP));
            bytes.push_back((uint8_t)(ctx.loop_start & 0xFF));
            bytes.push_back((uint8_t)((ctx.loop_start >> 8) & 0xFF));

            size_t end = bytes.size();
            bytes[ctx.if_patch]     = (uint8_t)(end & 0xFF);
            bytes[ctx.if_patch + 1] = (uint8_t)((end >> 8) & 0xFF);
            for (size_t p : ctx.break_patches) {
                bytes[p]     = (uint8_t)(end & 0xFF);
                bytes[p + 1] = (uint8_t)((end >> 8) & 0xFF);
            }
            continue;
        }

        // ---------- IF / ELIF / ELSE ----------
        if (cur.find("KID:IF") != std::string::npos) {
            HeapBlockCtx ctx; ctx.type = 0;
            size_t cp = cur.find("COND:");
            std::string cond_str;
            if (cp != std::string::npos) {
                cp += 5; size_t ce = cur.find("}", cp);
                cond_str = cur.substr(cp, ce == std::string::npos ? std::string::npos : ce - cp);
            }
            int cond_reg = -1;
            heap_emitCondition(cond_str, ra, bytes, cond_reg);
            bytes.push_back(static_cast<uint8_t>(OP_JUMP_IF_FALSE));
            bytes.push_back((uint8_t)cond_reg);
            ctx.if_patch = bytes.size();
            bytes.push_back(0x00); bytes.push_back(0x00);
            stack.push_back(ctx);
            continue;
        }
        if (cur.find("KID:ELIF") != std::string::npos) {
            if (stack.empty() || stack.back().type != 0) continue;
            HeapBlockCtx& ctx = stack.back();
            size_t cur_ip = bytes.size() + 3;
            bytes[ctx.if_patch]     = (uint8_t)(cur_ip & 0xFF);
            bytes[ctx.if_patch + 1] = (uint8_t)((cur_ip >> 8) & 0xFF);
            bytes.push_back(static_cast<uint8_t>(OP_JMP));
            ctx.end_jumps.push_back(bytes.size());
            bytes.push_back(0x00); bytes.push_back(0x00);
            size_t cp = cur.find("COND:");
            std::string cond_str;
            if (cp != std::string::npos) {
                cp += 5; size_t ce = cur.find("}", cp);
                cond_str = cur.substr(cp, ce == std::string::npos ? std::string::npos : ce - cp);
            }
            int cond_reg = -1;
            heap_emitCondition(cond_str, ra, bytes, cond_reg);
            bytes.push_back(static_cast<uint8_t>(OP_JUMP_IF_FALSE));
            bytes.push_back((uint8_t)cond_reg);
            ctx.if_patch = bytes.size();
            bytes.push_back(0x00); bytes.push_back(0x00);
            continue;
        }
        if (cur.find("KID:ELSE") != std::string::npos) {
            if (stack.empty() || stack.back().type != 0) continue;
            HeapBlockCtx& ctx = stack.back();
            size_t cur_ip = bytes.size() + 3;
            bytes[ctx.if_patch]     = (uint8_t)(cur_ip & 0xFF);
            bytes[ctx.if_patch + 1] = (uint8_t)((cur_ip >> 8) & 0xFF);
            bytes.push_back(static_cast<uint8_t>(OP_JMP));
            ctx.end_jumps.push_back(bytes.size());
            bytes.push_back(0x00); bytes.push_back(0x00);
            ctx.type = 1;
            continue;
        }

        // ---------- BREAK / CONTINUE ----------
        if (cur.find("KID:BREAK") != std::string::npos) {
            int t = (int)stack.size() - 1;
            while (t >= 0 && stack[t].type != 2 && stack[t].type != 3) --t;
            if (t >= 0) {
                bytes.push_back(static_cast<uint8_t>(OP_JMP));
                stack[t].break_patches.push_back(bytes.size());
                bytes.push_back(0x00); bytes.push_back(0x00);
            }
            continue;
        }
        if (cur.find("KID:CONTINUE") != std::string::npos) {
            int t = (int)stack.size() - 1;
            while (t >= 0 && stack[t].type != 2 && stack[t].type != 3) --t;
            if (t >= 0) {
                bytes.push_back(static_cast<uint8_t>(OP_JMP));
                stack[t].continue_patches.push_back(bytes.size());
                bytes.push_back(0x00); bytes.push_back(0x00);
            }
            continue;
        }

        // ---------- 赋值 ----------
        if (cur.find("PID:") != std::string::npos) {
            size_t vp = cur.find("{VID:");
            if (vp == std::string::npos) continue;
            char var = cur[vp + 5];
            size_t tp = cur.find("[3D]");
            if (tp == std::string::npos) continue;
            size_t es = tp + 4;
            size_t ee = cur.find("}MEID", es);
            if (ee == std::string::npos) ee = cur.size();
            std::string expr = cur.substr(es, ee - es);

            int r_val = -1;
            heap_emitOperand(expr, ra, bytes, r_val);

            heap_emit_var_write(var, r_val, ra, bytes);
            continue;
        }

        // ---------- 打印 ----------
        if (cur.find("FID:PRINT") != std::string::npos) {
            size_t ps = cur.find("FID:PRINT") + 9;
            while (ps < cur.size() && cur[ps] == ' ') ps++;
            size_t pe = cur.find("}}MEID");
            if (pe == std::string::npos) pe = cur.find("}MEID");
            if (pe == std::string::npos) continue;
            std::string args = cur.substr(ps, pe - ps);
            int r_val = -1;
            heap_emitOperand(args, ra, bytes, r_val);
            bytes.push_back(static_cast<uint8_t>(OP_PRINT));
            bytes.push_back((uint8_t)r_val);
            continue;
        }
    }

    bytes.push_back(static_cast<uint8_t>(OP_HALT));

    for (const auto& cs : g_call_sites) {
        auto it = g_func_table.find(cs.first);
        if (it != g_func_table.end()) {
            bytes[cs.second]     = (uint8_t)(it->second & 0xFF);
            bytes[cs.second + 1] = (uint8_t)((it->second >> 8) & 0xFF);
        } else {
            std::cerr << "[HEAP编译] 未定义的函数: " << cs.first << "\n";
        }
    }

    return bytes;
}