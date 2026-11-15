#pragma once

// ============================================================
// RANGE.inc —— 从 KID:FOR 的 AST 行里提取 start / end
//   支持 NID / FLT 字面量 + VID 变量
// ============================================================
void handleForRangeClause(const std::string& line,
                          int64_t& start_out, int64_t& end_out) {
    start_out = 0;
    end_out   = 0;

    // ---------- START: ----------
    size_t spos = line.find("START:");
    if (spos != std::string::npos) {
        size_t s = spos + 6;
        while (s < line.size() && line[s] == ' ') s++;

        if (line.compare(s, 4, "NID:") == 0) {
            size_t e = s + 4;
            while (e < line.size() && isdigit((unsigned char)line[e])) e++;
            std::string num = line.substr(s + 4, e - s - 4);
            size_t fnz = num.find_first_not_of('0');
            num = (fnz == std::string::npos) ? "0" : num.substr(fnz);
            if (!num.empty()) start_out = std::stoll(num);
        }
        else if (line.compare(s, 4, "FLT:") == 0) {
            size_t e = s + 4;
            while (e < line.size() &&
                   (isdigit((unsigned char)line[e]) || line[e] == '.')) e++;
            std::string num = line.substr(s + 4, e - s - 4);
            if (!num.empty()) start_out = (int64_t)std::stod(num);
        }
        else if (line.compare(s, 4, "VID:") == 0) {
            size_t e = s + 4;
            while (e < line.size() &&
                   (isalnum((unsigned char)line[e]) || line[e] == '_')) e++;
            std::string name = line.substr(s + 4, e - s - 4);
            std::string val = resolve_deep_value(name, main_memory);
            try { start_out = (int64_t)std::stod(val); } catch (...) { start_out = 0; }
        }
    }

    // ---------- END:（兼容 COUNT:） ----------
    size_t cpos = line.find("END:");
    if (cpos == std::string::npos) cpos = line.find("COUNT:");
    if (cpos != std::string::npos) {
        size_t s;
        if (line.compare(cpos, 6, "COUNT:") == 0) s = cpos + 6;
        else                                       s = cpos + 4;
        while (s < line.size() && line[s] == ' ') s++;

        if (line.compare(s, 4, "NID:") == 0) {
            size_t e = s + 4;
            while (e < line.size() && isdigit((unsigned char)line[e])) e++;
            std::string num = line.substr(s + 4, e - s - 4);
            size_t fnz = num.find_first_not_of('0');
            num = (fnz == std::string::npos) ? "0" : num.substr(fnz);
            if (!num.empty()) end_out = std::stoll(num);
        }
        else if (line.compare(s, 4, "FLT:") == 0) {
            size_t e = s + 4;
            while (e < line.size() &&
                   (isdigit((unsigned char)line[e]) || line[e] == '.')) e++;
            std::string num = line.substr(s + 4, e - s - 4);
            if (!num.empty()) end_out = (int64_t)std::stod(num);
        }
        else if (line.compare(s, 4, "VID:") == 0) {
            size_t e = s + 4;
            while (e < line.size() &&
                   (isalnum((unsigned char)line[e]) || line[e] == '_')) e++;
            std::string name = line.substr(s + 4, e - s - 4);
            std::string val = resolve_deep_value(name, main_memory);
            try { end_out = (int64_t)std::stod(val); } catch (...) { end_out = 0; }
        }
    }
}