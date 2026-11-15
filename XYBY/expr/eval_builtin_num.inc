// ============================================================
// 数值类内建
// ============================================================

// ============================================================
// 🚀 通用参数解析辅助：从 s 的 from_pos 开始找第一个参数
//    识别 VID / NID / FLT / SID 四种字面量
//    - 返回求值后的字符串
//    - next_pos 输出参数结束后的位置（供取第二个参数用）
// ============================================================
std::string builtin_arg_at(const std::string& s, size_t from_pos,
                           MemorySpace& mem, size_t* next_pos) {
    if (from_pos >= s.size()) {
        if (next_pos) *next_pos = std::string::npos;
        return "";
    }

    size_t best = std::string::npos;
    std::string kind;

    auto probe = [&](const std::string& tag, const char* k) {
        size_t p = s.find(tag, from_pos);
        if (p != std::string::npos && (best == std::string::npos || p < best)) {
            best = p;
            kind = k;
        }
    };
    probe("VID:",    "VID");
    probe("NID:",    "NID");
    probe("FLT:",    "FLT");
    probe("SID:22[", "SID");

    if (best == std::string::npos) {
        if (next_pos) *next_pos = std::string::npos;
        return "";
    }

    if (kind == "VID") {
        size_t e1 = 0;
        std::string var = peek_var_after(s, best, &e1);
        if (next_pos) *next_pos = e1;
        return resolve_deep_value(var, mem);
    }
    else if (kind == "NID") {
        size_t e = best + 4;
        if (e < s.size() && s[e] == '-') e++;
        while (e < s.size() && isdigit((unsigned char)s[e])) e++;
        if (next_pos) *next_pos = e;
        return s.substr(best + 4, e - (best + 4));
    }
    else if (kind == "FLT") {
        size_t e = best + 4;
        while (e < s.size() &&
               (isdigit((unsigned char)s[e]) || s[e] == '.' ||
                s[e] == '-' || s[e] == '+' ||
                s[e] == 'e' || s[e] == 'E')) e++;
        if (next_pos) *next_pos = e;
        return s.substr(best + 4, e - (best + 4));
    }
    else {
        size_t e = s.find("]22", best + 7);
        if (e == std::string::npos) {
            if (next_pos) *next_pos = s.size();
            return "";
        }
        if (next_pos) *next_pos = e + 3;
        return s.substr(best + 7, e - (best + 7));
    }
}

std::string builtin_first_arg(const std::string& s, MemorySpace& mem) {
    return builtin_arg_at(s, 0, mem, nullptr);
}

bool try_eval_builtin_num(const std::string& s, MemorySpace& mem, std::string& out) {
    if (s.rfind("FID:ABS", 0) == 0) {
        std::string val = builtin_first_arg(s, mem);
        try {
            double d = std::stod(val);
            if (d < 0) d = -d;
            out = format_double(d);
        } catch (...) { out = val; }
        return true;
    }
    if (s.rfind("FID:INT", 0) == 0) {
        std::string val = builtin_first_arg(s, mem);
        try { out = std::to_string((long long)std::stod(val)); }
        catch (...) { out = "0"; }
        return true;
    }
    if (s.rfind("FID:HEX", 0) == 0) {
        std::string val = builtin_first_arg(s, mem);
        try {
            long long n = std::stoll(val);
            std::ostringstream oss; oss << std::hex << n;
            out = oss.str();
        } catch (...) { out = val; }
        return true;
    }
    if (s.rfind("FID:BIN", 0) == 0 || s.rfind("FID:OCT", 0) == 0) {
        bool is_bin = (s.rfind("FID:BIN", 0) == 0);
        std::string val = builtin_first_arg(s, mem);
        try {
            long long n = std::stoll(val);
            if (is_bin) {
                std::string bits;
                if (n == 0) bits = "0";
                else {
                    unsigned long long u = (unsigned long long)n;
                    while (u) { bits = char('0' + (u & 1)) + bits; u >>= 1; }
                }
                out = bits;
            } else {
                std::ostringstream oss; oss << std::oct << n;
                out = oss.str();
            }
        } catch (...) { out = val; }
        return true;
    }
    if (s.rfind("FID:CHR", 0) == 0 || s.rfind("FID:ORD", 0) == 0) {
        bool is_chr = (s.rfind("FID:CHR", 0) == 0);
        std::string val = builtin_first_arg(s, mem);
        if (is_chr) {
            try {
                int code = std::stoi(val);
                if (code < 0) code = 0;
                if (code > 255) code = 255;
                out = std::string(1, (char)code);
            } catch (...) { out = ""; }
        } else {
            out = val.empty() ? "0" : std::to_string((int)(unsigned char)val[0]);
        }
        return true;
    }
    if (s.rfind("FID:ROUND", 0) == 0 ||
        s.rfind("FID:FLOOR", 0) == 0 ||
        s.rfind("FID:CEIL",  0) == 0) {
        std::string val = builtin_first_arg(s, mem);
        try {
            double d = std::stod(val), r;
            if      (s.rfind("FID:ROUND", 0) == 0) r = std::round(d);
            else if (s.rfind("FID:FLOOR", 0) == 0) r = std::floor(d);
            else                                    r = std::ceil(d);
            out = format_double(r);
        } catch (...) { out = val; }
        return true;
    }
    if (s.rfind("FID:MAX", 0) == 0 || s.rfind("FID:MIN", 0) == 0) {
        bool is_max = (s.rfind("FID:MAX", 0) == 0);
        size_t e1 = std::string::npos;
        std::string va = builtin_arg_at(s, 0, mem, &e1);
        if (va.empty()) { out = "0"; return true; }
        std::string vb;
        if (e1 != std::string::npos) vb = builtin_arg_at(s, e1, mem, nullptr);
        if (vb.empty()) { out = va; return true; }
        try {
            double da = std::stod(va), db = std::stod(vb);
            double r  = is_max ? (da > db ? da : db) : (da < db ? da : db);
            out = format_double(r);
        } catch (...) {
            out = is_max ? (va > vb ? va : vb) : (va < vb ? va : vb);
        }
        return true;
    }
    if (s.rfind("FID:POW", 0) == 0 || s.rfind("FID:SUM", 0) == 0) {
        size_t e1 = std::string::npos;
        std::string va = builtin_arg_at(s, 0, mem, &e1);
        if (va.empty()) { out = "0"; return true; }
        std::string vb;
        if (e1 != std::string::npos) vb = builtin_arg_at(s, e1, mem, nullptr);
        if (vb.empty()) { out = va; return true; }
        try {
            double da = std::stod(va), db = std::stod(vb);
            out = (s.rfind("FID:POW", 0) == 0)
                ? format_double(std::pow(da, db))
                : format_double(da + db);
        } catch (...) { out = "0"; }
        return true;
    }
    return false;
}