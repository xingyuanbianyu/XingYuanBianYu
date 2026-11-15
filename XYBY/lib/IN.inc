#pragma once

// ============================================================
// IN.inc —— 从 KID:FOR 的 AST 行里提取循环变量名
// ============================================================
std::string handleForInClause(const std::string& line) {
    size_t npos = line.find("NAME:VID:");
    if (npos == std::string::npos) return "";
    size_t s = npos + 9;
    size_t e = s;
    while (e < line.size() &&
           (isalnum((unsigned char)line[e]) || line[e] == '_')) e++;
    return line.substr(s, e - s);
}