// ============================================================
// 字符串类内建
// ⚠️ 依赖 eval_builtin_num.inc 里的 builtin_first_arg / builtin_arg_at
// ============================================================

void collect_builtin_args(const std::string& s, MemorySpace& mem,
                          std::vector<std::string>& args) {
    size_t p = 0;
    while (p < s.size()) {
        size_t next = std::string::npos;
        std::string val = builtin_arg_at(s, p, mem, &next);
        if (next == std::string::npos) break;
        args.push_back(val);
        if (next <= p) break;
        p = next;
    }
}

bool try_eval_builtin_str(const std::string& s, MemorySpace& mem, std::string& out) {
    if (s.rfind("FID:LEN", 0) == 0) {
        size_t p = s.find("VID:");
        if (p != std::string::npos) {
            std::string var_name = peek_var_after(s, p);
            Variable* var = find_variable(var_name, mem);
            if (var && var->type == "LIST") {
                int cnt = 1;
                for (size_t i = 4; i < var->value.size(); ++i)
                    if (var->value[i] == '\x01') cnt++;
                out = std::to_string(cnt);
                return true;
            }
            if (var) {
                out = std::to_string(resolve_deep_value(var_name, mem).size());
                return true;
            }
        }
        std::string val = builtin_first_arg(s, mem);
        out = std::to_string(val.size());
        return true;
    }
    if (s.rfind("FID:STRCAT", 0) == 0) {
        std::string result;
        size_t p = 0;
        while (p < s.size()) {
            size_t next = std::string::npos;
            std::string val = builtin_arg_at(s, p, mem, &next);
            if (next == std::string::npos) break;
            result += val;
            if (next <= p) break;
            p = next;
        }
        out = result;
        return true;
    }
    if (s.rfind("FID:STR", 0) == 0) {
        out = builtin_first_arg(s, mem);
        return true;
    }
    if (s.rfind("FID:UPPER", 0) == 0 || s.rfind("FID:LOWER", 0) == 0 ||
        s.rfind("FID:REVERSE", 0) == 0) {
        std::string val = builtin_first_arg(s, mem);
        if (s.rfind("FID:UPPER", 0) == 0) {
            for (char& c : val) c = (char)std::toupper((unsigned char)c);
            out = val;
        } else if (s.rfind("FID:LOWER", 0) == 0) {
            for (char& c : val) c = (char)std::tolower((unsigned char)c);
            out = val;
        } else {
            out = std::string(val.rbegin(), val.rend());
        }
        return true;
    }
    if (s.rfind("FID:STRIP", 0) == 0 ||
        s.rfind("FID:FIND",  0) == 0 ||
        s.rfind("FID:REPLACE", 0) == 0) {
        std::vector<std::string> args;
        collect_builtin_args(s, mem, args);
        if (args.empty()) { out = ""; return true; }
        if (s.rfind("FID:STRIP", 0) == 0) {
            const std::string& v = args[0];
            size_t a = v.find_first_not_of(" \t\r\n");
            size_t b = v.find_last_not_of(" \t\r\n");
            out = (a == std::string::npos) ? "" : v.substr(a, b - a + 1);
            return true;
        }
        if (s.rfind("FID:FIND", 0) == 0 && args.size() >= 2) {
            size_t pos = args[0].find(args[1]);
            out = (pos == std::string::npos) ? "-1" : std::to_string((long long)pos);
            return true;
        }
        if (s.rfind("FID:REPLACE", 0) == 0 && args.size() >= 3) {
            std::string result = args[0];
            size_t pos = 0;
            while ((pos = result.find(args[1], pos)) != std::string::npos) {
                result.replace(pos, args[1].size(), args[2]);
                pos += args[2].size();
            }
            out = result;
            return true;
        }
        out = "";
        return true;
    }
    if (s.rfind("FID:ASCII", 0) == 0 || s.rfind("FID:REPR", 0) == 0) {
        std::string val = builtin_first_arg(s, mem);
        if (s.rfind("FID:REPR", 0) == 0) {
            out = "\"" + val + "\"";
        } else {
            std::ostringstream oss;
            for (unsigned char c : val) {
                if (c >= 32 && c < 127) oss << c;
                else oss << "\\x" << std::hex << std::setw(2) << std::setfill('0') << (int)c;
            }
            out = oss.str();
        }
        return true;
    }
    return false;
}