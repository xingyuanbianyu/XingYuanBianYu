// =====================================================================
// lib/DECOMPILE.inc - 虚进制 → 数字串
// 语法：
//   decompile 0.00.0.00.00.0.0.0              → 输出 01011000
//   decompile 0.00.0.00.00.0.0.0 as y         → 存入变量 y
//   decompile "0.00.0.00.00.0.0.0" as y       → 同上（带引号也可以）
// 规则：
//   "." 分隔同一组
//   "," 或空格分隔组，组之间不插字符
//   token 长度 - 1 = 数字（0→0, 00→1, ..., 0000000000→9）
// =====================================================================

// 从虚进制文本还原成数字串
std::string _decompile_toDigits(const std::string& code) {
    std::string digits;
    std::string token;

    auto flush = [&]() {
        if (token.empty()) return;
        if (token.size() > 10) throw std::invalid_argument("token > 10 zeros");
        for (char c : token) if (c != '0') throw std::invalid_argument("token must be all zero");
        int d = (int)token.size() - 1;
        digits.push_back(char('0' + d));
        token.clear();
    };

    for (char c : code) {
        if (c == '0') {
            token += c;
        } else if (c == '.') {
            flush();
        } else if (c == ',' || c == ' ' || c == '\t') {
            flush();          // 组分隔符，只是断开 token，不额外插字符
        } else {
            throw std::invalid_argument(std::string("bad char: ") + c);
        }
    }
    flush();
    return digits;
}

// ---------- DECOMPILE 处理器 ----------
bool handleDecompile(const std::string& line, MemorySpace& mem) {
    if (line.find("KID:DECOMPILE") == std::string::npos) return false;

    // 抓 VALUE: 后面的 SID 节点内容
    size_t val_pos = line.find("VALUE:");
    if (val_pos == std::string::npos) {
        std::cerr << "DECOMPILE 错误: 缺少 VALUE 节点" << std::endl;
        return true;
    }
    size_t val_start = val_pos + 6;

    size_t as_pos   = line.find(" AS:VID:", val_start);
    size_t meid_pos = line.find("}MEID:",   val_start);
    if (meid_pos == std::string::npos) return true;

    size_t val_end = (as_pos != std::string::npos && as_pos < meid_pos)
                     ? as_pos : meid_pos;
    std::string value_node = trim(line.substr(val_start, val_end - val_start));

    // 提取 SID 内容
    std::string code;
    if (value_node.rfind("SID:22[", 0) == 0) {
        size_t e = value_node.find("]22");
        if (e == std::string::npos) return true;
        code = value_node.substr(7, e - 7);
    } else {
        code = value_node;   // 兜底：直接当虚进制文本
    }

    // 目标变量
    std::string as_var;
    if (as_pos != std::string::npos) {
        size_t vs = as_pos + 8;
        size_t ve = vs;
        while (ve < line.size() && (isalnum((unsigned char)line[ve]) || line[ve] == '_')) ve++;
        as_var = line.substr(vs, ve - vs);
    }

    try {
        std::string result = _decompile_toDigits(code);

        if (!as_var.empty()) {
            Variable v;
            v.type    = "STR";
            v.value   = result;
            v.address = allocate_address(as_var);
            if (mem.copy_history.empty()) mem.copy_history.push_back({});
            mem.copy_history.back()[as_var] = v;
            mem.primary_memory[as_var]      = v;
        } else {
            std::cout << result << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "DECOMPILE 异常: " << e.what() << std::endl;
    }

    return true;
}