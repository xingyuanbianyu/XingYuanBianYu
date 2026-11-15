// ============================================================
// lib0.inc —— AST 字符串 → 字节码
// 被 VMBY.cpp 引入，依赖 VMBY.cpp 里已有的：
//   emitOperand / compileAssign / compilePrint / parseCondition /
//   find_last_bin_op / g_func_table / g_call_sites / trim
//   g_class_set / g_var_class / g_current_class / g_current_parent
// ============================================================

std::vector<uint8_t> compileFromASTString(const std::string& ast) {
    std::vector<uint8_t> bytes;
    size_t inst_count = 0;

    g_func_table.clear();
    g_call_sites.clear();
    g_class_set.clear();
    g_var_class.clear();
    g_current_class.clear();
    g_current_parent.clear();      // 🚀

    // ============================================================
    // 🚀 自动注入异常处理程序
    // ============================================================
    bytes.push_back(static_cast<uint8_t>(OP_JMP));
    size_t main_jmp_patch = bytes.size();
    bytes.push_back(0x00);
    inst_count++;

    size_t handler_ip = inst_count;
    std::string err_msg = "系统异常中断";
    bytes.push_back(static_cast<uint8_t>(OP_PUSH_S));
    bytes.push_back((uint8_t)(err_msg.size() & 0xFF));
    bytes.push_back((uint8_t)((err_msg.size() >> 8) & 0xFF));
    for (char c : err_msg) bytes.push_back((uint8_t)c);
    inst_count++;

    bytes.push_back(static_cast<uint8_t>(OP_PRINT));
    inst_count++;

    bytes.push_back(static_cast<uint8_t>(OP_HALT));
    inst_count++;

    size_t main_ip = inst_count;
    bytes[main_jmp_patch] = (uint8_t)(main_ip & 0xFF);

    bytes.push_back(static_cast<uint8_t>(OP_REG));
    bytes.push_back(0);
    bytes.push_back((uint8_t)(handler_ip & 0xFF));
    inst_count++;

    bytes.push_back(static_cast<uint8_t>(OP_REG));
    bytes.push_back(1);
    bytes.push_back((uint8_t)(handler_ip & 0xFF));
    inst_count++;

    // ============================================================
    // 用户代码编译
    // ============================================================
    struct BlockCtx {
        int type = 0;
        size_t if_patch = 0;
        size_t loop_start = 0;
        char loop_var = 0;
        int  end_val = 0;
        std::vector<size_t> end_jumps;
        std::vector<size_t> break_patches;
        std::vector<size_t> continue_patches;
    };
    std::vector<BlockCtx> block_stack;

    std::stringstream ss(ast);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(ss, line)) {
        if (!line.empty()) lines.push_back(line);
    }

    for (size_t i = 0; i < lines.size(); i++) {
        std::string cur = lines[i];
        std::string trimmed = trim(cur);

        if (trimmed == "{") continue;

        // ---------- 🚀 类定义 ----------
        if (cur.find("KID:CLASS") != std::string::npos) {
            size_t ns = cur.find("NAME:SID:22[");
            if (ns != std::string::npos) {
                ns += 12;
                size_t ne = cur.find("]22", ns);
                if (ne != std::string::npos) {
                    g_current_class = cur.substr(ns, ne - ns);
                    g_class_set.insert(g_current_class);
                }
            }
            // 🚀 提取 PARENTS:SID:22[xxx]22
            g_current_parent.clear();
            size_t ps = cur.find("PARENTS:SID:22[");
            if (ps != std::string::npos) {
                ps += 15;
                size_t pe = cur.find("]22", ps);
                if (pe != std::string::npos) {
                    g_current_parent = cur.substr(ps, pe - ps);
                }
            }
            BlockCtx ctx;
            ctx.type = 5;
            block_stack.push_back(ctx);
            continue;
        }

        // ---------- 函数定义 ----------
        if (cur.find("DEF NAME:SID:") != std::string::npos) {
            size_t name_start = cur.find("NAME:SID:22[");
            if (name_start == std::string::npos) continue;
            name_start += 12;
            size_t name_end = cur.find("]22", name_start);
            if (name_end == std::string::npos) continue;
            std::string func_name = cur.substr(name_start, name_end - name_start);

            // 🚀 在类里则加 "类名." 前缀
            if (!g_current_class.empty()) {
                func_name = g_current_class + "." + func_name;
            }

            // 提取参数列表
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
                    if (vp != std::string::npos && vp + 4 < tok.size()) {
                        params.push_back(tok[vp + 4]);
                    }
                }
            }

            // ============================================================
            // 🚀 确定父方法：
            //   1) 优先 INHERIT:SID:22[父类]22（显式标记）
            //   2) 否则用 PARENTS + 同名方法（隐式继承）
            // ============================================================
            std::string raw_meth = func_name;
            size_t dot = raw_meth.find('.');
            if (dot != std::string::npos) raw_meth = raw_meth.substr(dot + 1);

            std::string parent_method = "";

            size_t inh_pos = cur.find("INHERIT:SID:22[");
            if (inh_pos != std::string::npos && !g_current_class.empty()) {
                inh_pos += 15;
                size_t inh_end = cur.find("]22", inh_pos);
                if (inh_end != std::string::npos) {
                    std::string parent_class = cur.substr(inh_pos, inh_end - inh_pos);
                    parent_method = parent_class + "." + raw_meth;
                }
            }
            else if (!g_current_parent.empty()) {
                std::string candidate = g_current_parent + "." + raw_meth;
                if (g_func_table.find(candidate) != g_func_table.end()) {
                    parent_method = candidate;
                }
            }

            // 生成 JMP 跳过函数体
            bytes.push_back(static_cast<uint8_t>(OP_JMP));
            size_t skip_patch = bytes.size();
            bytes.push_back(0x00);
            inst_count++;

            g_func_table[func_name] = inst_count;

            // 参数提取（逆序 STORE）
            for (auto it = params.rbegin(); it != params.rend(); ++it) {
                bytes.push_back(static_cast<uint8_t>(OP_STORE));
                bytes.push_back(static_cast<uint8_t>(*it));
                inst_count++;
            }

            // ============================================================
            // 🚀 注入父方法调用：
            //   1) 从变量里读回 self + 参数，压栈
            //   2) CALL 父方法 → R
            //   3) STORE 临时变量 0x01
            //   4) LOAD 临时变量 params.size() 次
            //   5) STORE 回参数变量（逆序）
            //    父方法入口也会做同样的事（如果有祖类），形成递归链
            // ============================================================
            if (!parent_method.empty()) {
                // 1) LOAD self + 参数（正序）
                for (char p : params) {
                    bytes.push_back(static_cast<uint8_t>(OP_LOAD));
                    bytes.push_back(static_cast<uint8_t>(p));
                    inst_count++;
                }
                // 2) CALL 父方法
                bytes.push_back(static_cast<uint8_t>(OP_CALL));
                size_t parent_patch = bytes.size();
                bytes.push_back(0x00);
                inst_count++;
                g_call_sites.push_back({parent_method, parent_patch});

                // 3) STORE 临时变量 0x01
                bytes.push_back(static_cast<uint8_t>(OP_STORE));
                bytes.push_back(0x01);
                inst_count++;

                // 4) LOAD 临时变量 N 次
                for (size_t k = 0; k < params.size(); ++k) {
                    bytes.push_back(static_cast<uint8_t>(OP_LOAD));
                    bytes.push_back(0x01);
                    inst_count++;
                }

                // 5) STORE 回参数变量（逆序）
                for (auto it = params.rbegin(); it != params.rend(); ++it) {
                    bytes.push_back(static_cast<uint8_t>(OP_STORE));
                    bytes.push_back(static_cast<uint8_t>(*it));
                    inst_count++;
                }

                std::cout << "[编译] 继承: " << func_name << " ← " << parent_method
                          << " (" << params.size() << " 个参数)\n";
            }

            BlockCtx ctx;
            ctx.type = 4;
            ctx.if_patch = skip_patch;
            block_stack.push_back(ctx);
            continue;
        }

        // ---------- 函数返回 ----------
        if (cur.find("KID:RETURN") != std::string::npos) {
            size_t ret_start = cur.find("KID:RETURN") + 10;
            size_t ret_end = cur.find("}", ret_start);
            if (ret_end == std::string::npos) ret_end = cur.size();
            std::string expr = trim(cur.substr(ret_start, ret_end - ret_start));

            if (!expr.empty()) {
                std::string op_tag;
                OpCode op_code = OP_NOP;
                size_t op_pos = find_last_bin_op(expr, op_tag, op_code);
                if (op_pos != std::string::npos) {
                    std::string left  = expr.substr(0, op_pos);
                    std::string right = expr.substr(op_pos + op_tag.length());
                    emitOperand(left,  bytes, inst_count);
                    emitOperand(right, bytes, inst_count);
                    bytes.push_back(static_cast<uint8_t>(op_code));
                    inst_count++;
                } else {
                    emitOperand(expr, bytes, inst_count);
                }
            } else {
                bytes.push_back(static_cast<uint8_t>(OP_PUSH));
                bytes.push_back(0);
                inst_count++;
            }

            bytes.push_back(static_cast<uint8_t>(OP_RET));
            inst_count++;
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

            bytes.push_back(static_cast<uint8_t>(OP_PUSH));
            bytes.push_back(static_cast<uint8_t>(start_val & 0xFF));
            bytes.push_back(static_cast<uint8_t>(OP_STORE));
            bytes.push_back(static_cast<uint8_t>(loop_var));
            inst_count += 2;

            BlockCtx ctx;
            ctx.type = 3;
            ctx.loop_var = loop_var;
            ctx.end_val = end_val;
            ctx.loop_start = inst_count;

            bytes.push_back(static_cast<uint8_t>(OP_LOAD));
            bytes.push_back(static_cast<uint8_t>(loop_var));
            bytes.push_back(static_cast<uint8_t>(OP_PUSH));
            bytes.push_back(static_cast<uint8_t>(end_val & 0xFF));
            bytes.push_back(static_cast<uint8_t>(OP_LESS));
            inst_count += 3;

            bytes.push_back(static_cast<uint8_t>(OP_JUMP_IF_FALSE));
            ctx.if_patch = bytes.size();
            bytes.push_back(0x00);
            inst_count++;

            block_stack.push_back(ctx);
            continue;
        }

        // ---------- ENDFOR ----------
        if (cur.find("KID:ENDFOR") != std::string::npos) {
            if (block_stack.empty()) continue;
            if (block_stack.back().type != 3) continue;
            BlockCtx ctx = block_stack.back();
            block_stack.pop_back();

            size_t increment_start = inst_count;
            for (size_t cp : ctx.continue_patches) {
                bytes[cp] = static_cast<uint8_t>(increment_start);
            }

            bytes.push_back(static_cast<uint8_t>(OP_LOAD));
            bytes.push_back(static_cast<uint8_t>(ctx.loop_var));
            bytes.push_back(static_cast<uint8_t>(OP_PUSH));
            bytes.push_back(1);
            bytes.push_back(static_cast<uint8_t>(OP_ADD));
            bytes.push_back(static_cast<uint8_t>(OP_STORE));
            bytes.push_back(static_cast<uint8_t>(ctx.loop_var));
            inst_count += 4;

            bytes.push_back(static_cast<uint8_t>(OP_JMP));
            bytes.push_back(static_cast<uint8_t>(ctx.loop_start));
            inst_count++;

            size_t end_pos_idx = inst_count;
            bytes[ctx.if_patch] = static_cast<uint8_t>(end_pos_idx);
            for (size_t bp : ctx.break_patches) {
                bytes[bp] = static_cast<uint8_t>(end_pos_idx);
            }
            continue;
        }

        // ---------- WHILE ----------
        if (cur.find("KID:WHILE") != std::string::npos) {
            BlockCtx ctx;
            ctx.type = 2;
            ctx.loop_start = inst_count;
            if (!parseCondition(cur, bytes, inst_count)) continue;
            bytes.push_back(static_cast<uint8_t>(OP_JUMP_IF_FALSE));
            ctx.if_patch = bytes.size();
            bytes.push_back(0x00);
            inst_count++;
            block_stack.push_back(ctx);
            continue;
        }

        // ---------- IF ----------
        if (cur.find("KID:IF") != std::string::npos) {
            BlockCtx ctx;
            ctx.type = 0;
            if (!parseCondition(cur, bytes, inst_count)) continue;
            bytes.push_back(static_cast<uint8_t>(OP_JUMP_IF_FALSE));
            ctx.if_patch = bytes.size();
            bytes.push_back(0x00);
            inst_count++;
            block_stack.push_back(ctx);
            continue;
        }

        // ---------- ELIF ----------
        if (cur.find("KID:ELIF") != std::string::npos) {
            if (block_stack.empty()) continue;
            if (block_stack.back().type != 0) continue;
            size_t elif_start = inst_count + 1;
            bytes[block_stack.back().if_patch] = static_cast<uint8_t>(elif_start);
            bytes.push_back(static_cast<uint8_t>(OP_JMP));
            block_stack.back().end_jumps.push_back(bytes.size());
            bytes.push_back(0x00);
            inst_count++;
            if (!parseCondition(cur, bytes, inst_count)) continue;
            bytes.push_back(static_cast<uint8_t>(OP_JUMP_IF_FALSE));
            block_stack.back().if_patch = bytes.size();
            bytes.push_back(0x00);
            inst_count++;
            continue;
        }

        // ---------- ELSE ----------
        if (cur.find("KID:ELSE") != std::string::npos) {
            if (block_stack.empty()) continue;
            if (block_stack.back().type != 0) continue;
            size_t else_start = inst_count + 1;
            bytes[block_stack.back().if_patch] = static_cast<uint8_t>(else_start);
            bytes.push_back(static_cast<uint8_t>(OP_JMP));
            block_stack.back().end_jumps.push_back(bytes.size());
            bytes.push_back(0x00);
            inst_count++;
            block_stack.back().type = 1;
            continue;
        }

        // ---------- CONTINUE ----------
        if (cur.find("KID:CONTINUE") != std::string::npos) {
            int target_idx = block_stack.size() - 1;
            while (target_idx >= 0 &&
                   block_stack[target_idx].type != 2 &&
                   block_stack[target_idx].type != 3) target_idx--;
            if (target_idx >= 0) {
                bytes.push_back(static_cast<uint8_t>(OP_JMP));
                block_stack[target_idx].continue_patches.push_back(bytes.size());
                bytes.push_back(0x00);
                inst_count++;
            }
            continue;
        }

        // ---------- BREAK ----------
        if (cur.find("KID:BREAK") != std::string::npos) {
            int target_idx = block_stack.size() - 1;
            while (target_idx >= 0 &&
                   block_stack[target_idx].type != 2 &&
                   block_stack[target_idx].type != 3) target_idx--;
            if (target_idx >= 0) {
                bytes.push_back(static_cast<uint8_t>(OP_JMP));
                block_stack[target_idx].break_patches.push_back(bytes.size());
                bytes.push_back(0x00);
                inst_count++;
            }
            continue;
        }

        // ---------- ENDWHILE ----------
        if (cur.find("KID:ENDWHILE") != std::string::npos) {
            if (block_stack.empty()) continue;
            if (block_stack.back().type != 2) continue;
            BlockCtx ctx = block_stack.back();
            block_stack.pop_back();

            for (size_t cp : ctx.continue_patches) {
                bytes[cp] = static_cast<uint8_t>(ctx.loop_start);
            }

            bytes.push_back(static_cast<uint8_t>(OP_JMP));
            bytes.push_back(static_cast<uint8_t>(ctx.loop_start));
            inst_count++;
            for (size_t pp : ctx.break_patches) {
                bytes[pp] = static_cast<uint8_t>(inst_count);
            }
            ctx.break_patches.clear();
            bytes[ctx.if_patch] = static_cast<uint8_t>(inst_count);
            continue;
        }

        // ---------- } ----------
        if (trimmed == "}") {
            bool next_is_branch = false;
            if (i + 1 < lines.size()) {
                if (lines[i + 1].find("KID:ELSE") != std::string::npos ||
                    lines[i + 1].find("KID:ELIF") != std::string::npos) {
                    next_is_branch = true;
                }
            }
            if (next_is_branch) continue;

            if (!block_stack.empty()) {
                BlockCtx ctx = block_stack.back();

                if (ctx.type == 5) {
                    block_stack.pop_back();
                    g_current_class.clear();
                    g_current_parent.clear();      // 🚀
                    continue;
                }

                if (ctx.type == 4) {
                    block_stack.pop_back();
                    bytes.push_back(static_cast<uint8_t>(OP_PUSH));
                    bytes.push_back(0);
                    inst_count++;
                    bytes.push_back(static_cast<uint8_t>(OP_RET));
                    inst_count++;
                    bytes[ctx.if_patch] = static_cast<uint8_t>(inst_count);
                    continue;
                }

                if (ctx.type != 2 && ctx.type != 3) {
                    block_stack.pop_back();
                    size_t end_pos_idx = inst_count;
                    for (size_t jp : ctx.end_jumps) {
                        bytes[jp] = static_cast<uint8_t>(end_pos_idx);
                    }
                    if (ctx.type == 0) {
                        bytes[ctx.if_patch] = static_cast<uint8_t>(end_pos_idx);
                    }
                }
            }
            continue;
        }

        // ---------- 赋值 ----------
        if (cur.find("PID:") != std::string::npos) {
            compileAssign(cur, bytes, inst_count);
            continue;
        }

        // ---------- 打印 ----------
        if (cur.find("FID:PRINT") != std::string::npos) {
            compilePrint(cur, bytes, inst_count);
            continue;
        }
    }

    bytes.push_back(static_cast<uint8_t>(OP_HALT));
    inst_count++;

    // 回填所有 CALL
    for (const auto& cs : g_call_sites) {
        auto it = g_func_table.find(cs.first);
        if (it != g_func_table.end()) {
            bytes[cs.second] = static_cast<uint8_t>(it->second);
        } else {
            std::cerr << "[编译错误] 未定义的函数: " << cs.first << "\n";
        }
    }

    return bytes;
}