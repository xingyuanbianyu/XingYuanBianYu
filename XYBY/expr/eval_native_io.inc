// ============================================================
// 原生 I/O 内建：readfile / writefile / split / replace
// 返回 true 表示已处理
// ============================================================
bool try_eval_native_io(const std::string& expr, const std::string& fname,
                        MemorySpace& mem, std::string& out) {
    // --- readfile ---
    if (fname == "readfile") {
        size_t v = expr.find("VID:");
        if (v != std::string::npos) {
            size_t e0 = v + 4;
            while (e0 < expr.size() &&
                   (isalnum((unsigned char)expr[e0]) || expr[e0] == '_' || expr[e0] == '.')) e0++;
            std::string filename = expr.substr(v + 4, e0 - v - 4);
            if (filename.size() >= 2 && filename.front() == '"' && filename.back() == '"')
                filename = filename.substr(1, filename.size() - 2);

            std::ifstream ifs(filename);
            if (!ifs.is_open()) { out = "None"; return true; }
            std::stringstream buf; buf << ifs.rdbuf(); ifs.close();
            out = buf.str();
        } else {
            out = "";
        }
        return true;
    }

    // --- writefile ---
    if (fname == "writefile") {
        size_t v1 = expr.find("VID:");
        size_t v2 = expr.find("VID:", v1 + 4);
        if (v1 != std::string::npos && v2 != std::string::npos) {
            size_t e1 = v1 + 4;
            while (e1 < expr.size() &&
                   (isalnum((unsigned char)expr[e1]) || expr[e1] == '_' || expr[e1] == '.')) e1++;
            std::string filename = expr.substr(v1 + 4, e1 - v1 - 4);
            if (filename.size() >= 2 && filename.front() == '"' && filename.back() == '"')
                filename = filename.substr(1, filename.size() - 2);

            size_t e2 = v2 + 4;
            while (e2 < expr.size() &&
                   (isalnum((unsigned char)expr[e2]) || expr[e2] == '_' || expr[e2] == '"' ||
                    expr[e2] == ' ' || expr[e2] == '\\' || expr[e2] == '.')) e2++;
            std::string content = expr.substr(v2 + 4, e2 - v2 - 4);
            if (content.size() >= 2 && content.front() == '"' && content.back() == '"')
                content = content.substr(1, content.size() - 2);

            std::ofstream ofs(filename);
            if (ofs.is_open()) { ofs << content; ofs.close(); out = ""; }
            else               out = "Error";
        } else {
            out = "";
        }
        return true;
    }

    // --- split ---
    if (fname == "split") {
        size_t v1 = expr.find("VID:");
        size_t v2 = expr.find("VID:", v1 + 4);
        if (v1 != std::string::npos && v2 != std::string::npos) {
            size_t e1;
            std::string str = resolve_deep_value(peek_var_after(expr, v1, &e1), mem);
            std::string delim = resolve_deep_value(peek_var_after(expr, v2), mem);
            if (delim.empty()) delim = "\n";

            std::string result;
            size_t pos = 0;
            while ((pos = str.find(delim)) != std::string::npos) {
                result += str.substr(0, pos) + "\x1F";
                str.erase(0, pos + delim.length());
            }
            result += str;
            out = result;
        } else {
            out = "";
        }
        return true;
    }

    // --- replace（原生，区别于 FID:REPLACE） ---
    if (fname == "replace") {
        size_t v1 = expr.find("VID:");
        size_t v2 = expr.find("VID:", v1 + 4);
        size_t v3 = expr.find("VID:", v2 + 4);
        if (v1 != std::string::npos && v2 != std::string::npos && v3 != std::string::npos) {
            std::string str     = resolve_deep_value(peek_var_after(expr, v1), mem);
            std::string old_sub = resolve_deep_value(peek_var_after(expr, v2), mem);
            std::string new_sub = resolve_deep_value(peek_var_after(expr, v3), mem);

            size_t pos = 0;
            while ((pos = str.find(old_sub, pos)) != std::string::npos) {
                str.replace(pos, old_sub.length(), new_sub);
                pos += new_sub.length();
            }
            out = str;
        } else {
            out = "";
        }
        return true;
    }

    return false;
}