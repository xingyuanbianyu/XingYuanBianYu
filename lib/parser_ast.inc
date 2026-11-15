#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdio>
// 辅助函数：将条件字符串按空格切分成 Token
std::vector<std::string> tokenize_cond(const std::string& cond) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(cond);
    while (tokenStream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}
// ============================================================
// 🚀 辅助函数：TRF 解析所需
// ============================================================
std::string trim_str(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::vector<std::string> split_by_space(const std::string& str) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

// ============================================================
// 🚀 修正后的 TRF AST 生成函数
// ============================================================
std::string generate_trf_core_ast(const std::string& expr_body) {
    std::vector<std::string> tokens = split_by_space(expr_body);
    if (tokens.size() < 3 || tokens.size() % 2 == 0) {
        return "ERROR:TRF语法错误";
    }
    std::string ops_ast = "";
    std::string vars_ast = "";
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i % 2 == 0) {
            vars_ast += "{VID:" + tokens[i] + "}";
            if (i < tokens.size() - 1) vars_ast += ",";
        } else {
            ops_ast += "{VID:" + tokens[i] + "}"; 
            if (i < tokens.size() - 2) ops_ast += ",";
        }
    }
    return "{KID:TRF OPS:[" + ops_ast + "] VARS:[" + vars_ast + "]}";
}
// ============================================================

// =================================================================
// parser_ast.inc —— 源码 → AST 核心转换器
// =================================================================
std::string strip_cooid_placeholders(const std::string& content) {
    if (content.rfind("COOID_", 0) == 0) {
        bool is_pure_cooid = true;
        for (size_t j = 6; j < content.size(); ++j) {
            if (!isdigit((unsigned char)content[j])) {
                is_pure_cooid = false;
                break;
            }
        }
        if (is_pure_cooid) return "";
    }
    return trim(content);
}
uint32_t allocate_address(const std::string& name);

std::vector<std::string> parse_source_to_ast(
        const std::string& code,
        std::map<std::string, std::vector<std::string>>& modules,
        std::set<std::string>& loading_modules) {

    std::set<std::string> included_files;
    std::string expanded_code = expand_includes(code, included_files);
    expanded_code = process_block_comments(expanded_code);

    std::vector<std::string> ast_lines;
    ast_lines.push_back("{");
    int current_ast_level = 1;

    // ============================================================
    // 🚀 词法：把 expanded_code 切成 (stmt, ctrl, indent) 序列
    //    ctrl ∈ {'\\', '$', '&', 0}
    //    indent 只用于兼容纯缩进输入（符号式输入 indent 恒为 0）
    // ============================================================
    struct LTok { std::string stmt; char ctrl; int indent; };
    std::vector<LTok> toks;
    {
        std::string cur;
        bool in_str = false;
        char q = 0;
        int line_indent = 0;
        bool at_line_start = true;
        for (size_t i = 0; i < expanded_code.size(); ++i) {
            char c = expanded_code[i];
            if (c == '\n') {
                if (!trim_str(cur).empty())
                    toks.push_back({trim_str(cur), 0, line_indent});
                cur.clear();
                at_line_start = true;
                line_indent = 0;
                continue;
            }
            if (at_line_start) {
                if (c == ' ' || c == '\t') { line_indent++; continue; }
                at_line_start = false;
            }
            if (in_str) {
                cur += c;
                if (c == '\\' && i + 1 < expanded_code.size()) {
                    cur += expanded_code[++i];
                    continue;
                }
                if (c == q) in_str = false;
                continue;
            }
            if (c == '"' || c == '\'') {
                in_str = true; q = c; cur += c; continue;
            }
            if (c == '\\' || c == '$' || c == '&') {
                toks.push_back({trim_str(cur), c, line_indent});
                cur.clear();
                continue;
            }
            cur += c;
        }
        if (!trim_str(cur).empty())
            toks.push_back({trim_str(cur), 0, line_indent});
    }

    // ============================================================
    // 🚀 块栈：符号驱动的嵌套结构
    // ============================================================
    struct Block { char kind; int indent; };
    std::vector<Block> block_stack;

    auto open_block = [&](char kind, int indent) {
        ast_lines.push_back(std::string(current_ast_level * 4, ' ') + "{");
        block_stack.push_back({kind, indent});
        current_ast_level++;
    };
    auto close_block = [&]() {
        if (block_stack.empty()) return;
        Block b = block_stack.back(); block_stack.pop_back();
        current_ast_level--;
        ast_lines.push_back(std::string(current_ast_level * 4, ' ') + "}");
        if (b.kind == 'F')
            ast_lines.push_back(std::string(current_ast_level * 4, ' ') + "ID:{KID:ENDFOR}MEID:LID:0");
        if (b.kind == 'W')
            ast_lines.push_back(std::string(current_ast_level * 4, ' ') + "ID:{KID:ENDWHILE}MEID:LID:0");
    };

    auto is_reserved_name = [](const std::string& n) {
        static const std::set<std::string> kw = {
            "IF","ELIF","ELSE","FOR","WHILE","DEF","RETURN","IMPORT","USE",
            "AS","PRINT","INPUT","INT","FLOAT","STRING","TRUE","FALSE","NONE",
            "AND","OR","NOT","IN","IS","PASS","BREAK","CONTINUE","GLOBAL","NONLOCAL",
            "LET","CONST","FROM","ASYNC","AWAIT","DEL","BUILD","DECOMPILE","OPEN","LEN",
            "ABS", "STR", "BOOL", "MAX", "MIN", "HEX", "BIN", "OCT", "CHR", "ORD", "ROUND",  "LOOR",
            "CEIL", "ROUND", "FLOOR", "CEIL", "ASCII","HASH","ID","REPR","HELP","POW","SUM",
            "STRIP","FIND","REPLACE", "WITH", "BIN7_DECODE", "XYCREATE", "EXECVM", "STRCAT",
            "TRF"
        };
        return kw.count(n) > 0;
    };

    // ============================================================
    // 🚀 emit_statement：语句识别，只输出 AST 行 + 返回块类型
    //    返回值：
    //      0 = 普通语句 / 不需要开块
    //      'F' = FOR    需要开层
    //      'W' = WHILE  需要开层
    //      'I' = IF/ELIF/ELSE 需要开层
    //      'D' = DEF    需要开层
    //      'C' = CLASS  需要开层
    //      'O' = WITH   需要开层
    // ============================================================
    auto emit_statement = [&](const std::string& raw_content, int src_line) -> char {
        std::string content = strip_cooid_placeholders(
            strip_inline_comments(trim(raw_content), modules)
        );
        if (content.empty()) return 0;

        // ---------- 操作码识别 ----------
        {
            auto lookup_keyword = [&](const std::string& raw) -> const std::string* {
                auto it = KEYWORD_MAP.find(raw);
                if (it != KEYWORD_MAP.end()) return &it->second;
                std::string upper = raw, lower = raw, cap = raw;
                for (char& c : upper) c = (char)toupper((unsigned char)c);
                for (char& c : lower) c = (char)tolower((unsigned char)c);
                if (!cap.empty()) cap[0] = (char)toupper((unsigned char)cap[0]);
                it = KEYWORD_MAP.find(upper); if (it != KEYWORD_MAP.end()) return &it->second;
                it = KEYWORD_MAP.find(lower); if (it != KEYWORD_MAP.end()) return &it->second;
                it = KEYWORD_MAP.find(cap);   if (it != KEYWORD_MAP.end()) return &it->second;
                return nullptr;
            };
            size_t first_end = content.find_first_of(" \t(:");
            std::string first = (first_end == std::string::npos) ? content : content.substr(0, first_end);
            if (!first.empty() && (unsigned char)first[0] > 127) {
                content = map_chinese_to_english(content);
                first_end = content.find_first_of(" \t(:");
                first = (first_end == std::string::npos) ? content : content.substr(0, first_end);
            }
            std::string rest = (first_end == std::string::npos) ? "" : content.substr(first_end);
            const std::string* mapped = lookup_keyword(first);
            if (mapped != nullptr) content = *mapped + rest;
        }
        std::string pad(current_ast_level * 4, ' ');

        // ---------- GLOBAL. 前缀 ----------
        bool is_global_decl = false;
        if (content.rfind("GLOBAL.", 0) == 0) {
            is_global_decl = true;
            content = trim(content.substr(7));
        }

        bool is_const_decl = false;
        if (content.rfind("CONST ", 0) == 0) {
            is_const_decl = true;
            content = trim(content.substr(6));
        } else if (content.rfind("LET ", 0) == 0) {
            content = trim(content.substr(4));
        }

        // ---------- init / int / str 前缀 ----------
        bool is_init = false;
        std::string var_type = "";
        bool stripped = true;
        while (stripped) {
            stripped = false;
            if (content.rfind("INIT ", 0) == 0 || content.rfind("init ", 0) == 0) {
                is_init = true; content = trim(content.substr(5)); stripped = true;
            } else if (content.rfind("INT ", 0) == 0 || content.rfind("int ", 0) == 0) {
                var_type = "INT"; content = trim(content.substr(4)); stripped = true;
            } else if (content.rfind("STR ", 0) == 0 || content.rfind("str ", 0) == 0) {
                var_type = "STR"; content = trim(content.substr(4)); stripped = true;
            }
        }
        std::string decl_flags = "";
        if (is_init) decl_flags += " INIT:TRUE";
        if (!var_type.empty()) decl_flags += " TYPE:" + var_type;

        // ---------- ASYNC DEF ----------
        bool is_async_func = false;
        if (content.rfind("ASYNC DEF ", 0) == 0) {
            is_async_func = true;
            content = trim(content.substr(10));
        }

        // ---------- FROM ... IMPORT ... ----------
        {
            bool is_from_import = false;
            std::string module_name = "";
            std::string funcs_str = "";
            if (content.rfind("FROM ", 0) == 0) {
                is_from_import = true;
                std::string rest = trim(content.substr(5));
                size_t import_pos = rest.find(" IMPORT ");
                if (import_pos != std::string::npos) {
                    std::string mod_part = trim(rest.substr(0, import_pos));
                    std::string func_part = trim(rest.substr(import_pos + 8));
                    if (mod_part.size() >= 2 && mod_part.front() == '"' && mod_part.back() == '"')
                        mod_part = mod_part.substr(1, mod_part.size()-2);
                    module_name = mod_part;
                    funcs_str = func_part;
                }
            }
            if (is_from_import && !module_name.empty() && !funcs_str.empty()) {
                if (modules.find(module_name) == modules.end()) {
                    if (!loading_modules.count(module_name)) {
                        std::string src = read_module_source(module_name);
                        if (!src.empty()) {
                            loading_modules.insert(module_name);
                            modules[module_name] = parse_source_to_ast(src, modules, loading_modules);
                            loading_modules.erase(module_name);
                        } else {
                            modules[module_name] = {};
                        }
                    }
                }
                ast_lines.push_back(
                    pad + "ID:{KID:FROM>MOD:SID:22[" + module_name + "]22>FUNCS:SID:22[" + funcs_str + "]22}MEID:LID:" + std::to_string(src_line));
                return 0;
            }
        }

        // ---------- IMPORT / USE ----------
        {
            std::string k1 = "IMPORT ", k2 = "USE ";
            std::string matched_kw;
            if (content.rfind(k1, 0) == 0)      matched_kw = k1;
            else if (content.rfind(k2, 0) == 0) matched_kw = k2;

            if (!matched_kw.empty()) {
                std::string rest = trim(content.substr(matched_kw.size()));
                while (!rest.empty() && (rest.back() == ';' || rest.back() == ' ')) rest.pop_back();
                if (rest.size() >= 2 &&
                    ((rest.front() == '"' && rest.back() == '"') ||
                     (rest.front() == '\'' && rest.back() == '\''))) {
                    rest = rest.substr(1, rest.size() - 2);
                }
                std::string module_name = rest;
                size_t as_pos = rest.find(" AS ");
                if (as_pos != std::string::npos) module_name = trim(rest.substr(0, as_pos));
                if (module_name.empty()) return 0;

                if (modules.find(module_name) == modules.end()) {
                    if (!loading_modules.count(module_name)) {
                        std::string src = read_module_source(module_name);
                        if (!src.empty()) {
                            loading_modules.insert(module_name);
                            modules[module_name] = parse_source_to_ast(src, modules, loading_modules);
                            loading_modules.erase(module_name);
                        } else {
                            modules[module_name] = {};
                        }
                    }
                }
                ast_lines.push_back(
                    pad + "ID:{IMP:" + module_name + "}MEID:LID:" + std::to_string(src_line));
                return 0;
            }
        }

        // ---------- DEF ----------
        {
            std::string content_work = content;
            bool is_async_local = is_async_func;
            if (content_work.size() >= 6) {
                std::string prefix = content_work.substr(0, 6);
                std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
                if (prefix == "async ") {
                    is_async_local = true;
                    content_work = trim(content_work.substr(6));
                }
            }
            if (content_work.rfind("DEF ", 0) == 0 ||
                content_work.rfind("def ", 0) == 0 ||
                content_work.find("(def ", 0) != std::string::npos ||
                content_work.find("(DEF ", 0) != std::string::npos) {

                std::string inherit_from;
                std::string work = content_work;

                size_t first_ws = work.find_first_not_of(" \t");
                if (first_ws != std::string::npos) {
                    std::string head = work.substr(first_ws, 4);
                    if (head != "def " && head != "DEF ") {
                        size_t inner_def = work.find("def ");
                        size_t inner_DEF = work.find("DEF ");
                        size_t def_pos = std::min(
                            inner_def == std::string::npos ? SIZE_MAX : inner_def,
                            inner_DEF == std::string::npos ? SIZE_MAX : inner_DEF
                        );
                        if (def_pos != SIZE_MAX && def_pos > first_ws) {
                            inherit_from = trim(work.substr(first_ws, def_pos - first_ws));
                            while (!inherit_from.empty() && inherit_from.back() == '(')
                                inherit_from.pop_back();
                            inherit_from = trim(inherit_from);
                            work = work.substr(def_pos);
                        }
                    }
                }
                std::string rest = work.substr(4);
                while (!rest.empty() && (rest.back() == ':' || rest.back() == ' '))
                    rest.pop_back();
                size_t lp = rest.find('(');
                size_t rp = rest.rfind(')');
                if (lp == std::string::npos || rp == std::string::npos || rp < lp) return 0;

                std::string fname = trim(rest.substr(0, lp));
                std::string params_str = trim(rest.substr(lp + 1, rp - lp - 1));

                std::vector<std::string> params;
                if (!params_str.empty()) {
                    std::string tmp;
                    for (char c : params_str) {
                        if (c == ',') {
                            if (!tmp.empty()) { params.push_back(trim(tmp)); tmp.clear(); }
                        } else tmp += c;
                    }
                    if (!tmp.empty()) params.push_back(trim(tmp));
                }
                std::string params_node;
                for (size_t k = 0; k < params.size(); ++k) {
                    if (k > 0) params_node += ",";
                    params_node += "VID:" + params[k];
                }
                std::string markers = "";
                if (is_async_local) markers += "ASYNC:TRUE ";
                if (is_global_decl) markers += "GLOBAL:TRUE ";
                if (!inherit_from.empty())
                    markers += "INHERIT:SID:22[" + inherit_from + "]22 ";

                ast_lines.push_back(
                    pad + "ID:{KID:DEF NAME:SID:22[" + fname + "]22 " + markers + "PARAMS:" +
                    params_node + "}MEID:LID:" + std::to_string(src_line));
                return 'D';
            }
        }

        // ---------- CLASS ----------
        if (content.rfind("CLASS ", 0) == 0) {
            std::string rest = content.substr(6);
            while (!rest.empty() && (rest.back() == ':' || rest.back() == ' ')) rest.pop_back();
            std::string parents_list;
            size_t paren = rest.find('(');
            size_t rparen = rest.rfind(')');
            if (paren != std::string::npos && rparen != std::string::npos && rparen > paren) {
                parents_list = trim(rest.substr(paren + 1, rparen - paren - 1));
                rest = rest.substr(0, paren);
            }
            std::string class_name = trim(rest);
            if (class_name.empty()) return 0;
            std::string node = "ID:{KID:CLASS NAME:SID:22[" + class_name + "]22";
            if (!parents_list.empty())
                node += " PARENTS:SID:22[" + parents_list + "]22";
            node += "}MEID:LID:" + std::to_string(src_line);
            ast_lines.push_back(pad + node);
            return 'C';
        }

        // ---------- FOR ----------
        if (content.rfind("FOR ", 0) == 0) {
            std::string rest = content.substr(4);
            while (!rest.empty() && (rest.back() == ':' || rest.back() == ' ')) rest.pop_back();
            size_t in_pos = rest.find(" IN ");
            if (in_pos == std::string::npos) in_pos = rest.find(" in ");
            if (in_pos == std::string::npos) return 0;

            std::string var = trim(rest.substr(0, in_pos));
            std::string rhs = trim(rest.substr(in_pos + 4));
            if (var.empty() || rhs.empty()) return 0;

            if ((rhs.rfind("range(", 0) == 0 || rhs.rfind("RANGE(", 0) == 0)
                && rhs.back() == ')') {
                std::string inside = trim(rhs.substr(6, rhs.size() - 7));
                std::string s_str, e_str;
                size_t comma = inside.find(',');
                if (comma != std::string::npos) {
                    s_str = trim(inside.substr(0, comma));
                    e_str = trim(inside.substr(comma + 1));
                } else {
                    s_str = "0"; e_str = inside;
                }
                std::string start_node = parse_expression(s_str);
                std::string end_node   = parse_expression(e_str);
                std::string node_in    = "{KID:IN NAME:VID:" + var + "}";
                std::string node_range = "{FID:RANGE START:" + start_node + " END:" + end_node + "}";
                std::string node_for   = "{KID:FOR}" + node_in + node_range;
                ast_lines.push_back(
                    pad + "ID:" + node_for + "MEID:LID:" + std::to_string(src_line));
                return 'F';
            }
            {
                std::string src_node = parse_expression(rhs);
                if (src_node.empty()) src_node = "VID:" + rhs;
                std::string node_in   = "{KID:IN NAME:VID:" + var + "}";
                std::string node_iter = "{FID:ITER SRC:" + src_node + "}";
                std::string node_for  = "{KID:FOR}" + node_in + node_iter;
                ast_lines.push_back(
                    pad + "ID:" + node_for + "MEID:LID:" + std::to_string(src_line));
                return 'F';
            }
        }

        // ---------- WHILE ----------
        if (content.find("WHILE ", 0) == 0) {
            std::string cond = content.substr(6);
            while (!cond.empty() && (cond.back() == ':' || cond.back() == ' ')) cond.pop_back();
            cond = trim(cond);
            std::string cond_node = cond_to_ast(cond);
            if (cond_node.empty()) cond_node = "RAW:" + cond;
            ast_lines.push_back(
                pad + "ID:{KID:WHILE COND:" + cond_node + "}MEID:LID:" + std::to_string(src_line));
            return 'W';
        }

        // ---------- IF / ELIF ----------
        if (content.rfind("IF ", 0) == 0 || content.rfind("ELIF ", 0) == 0) {
            std::string kw = (content.rfind("IF ", 0) == 0) ? "IF" : "ELIF";
            std::string cond = trim(content.substr(kw.length()));
            if (!cond.empty() && cond.back() == ':') cond.pop_back();
            cond = trim(cond);

            std::string opcode = (kw == "IF") ? "KID:IF" : "KID:ELIF";

            auto parse_logic_expr = [&](auto&& self, const std::string& str) -> std::string {
                std::string s = trim(str);
                if (s.empty()) return "";
                if (s.rfind("trf ", 0) == 0) {
                    return generate_trf_core_ast(trim(s.substr(4)));
                }
                size_t or_pos = s.find(" or ");
                if (or_pos == std::string::npos) or_pos = s.find(" OR ");
                if (or_pos != std::string::npos) {
                    std::string left = s.substr(0, or_pos);
                    std::string right = s.substr(or_pos + 4);
                    return "(" + self(self, left) + "[OR]" + self(self, right) + ")";
                }
                size_t and_pos = s.find(" and ");
                if (and_pos == std::string::npos) and_pos = s.find(" AND ");
                if (and_pos != std::string::npos) {
                    std::string left = s.substr(0, and_pos);
                    std::string right = s.substr(and_pos + 5);
                    return "(" + self(self, left) + "[AND]" + self(self, right) + ")";
                }
                if (s.rfind("not ", 0) == 0 || s.rfind("NOT ", 0) == 0) {
                    std::string operand = s.substr(4);
                    return "[NOT](" + self(self, operand) + ")";
                }
                std::string res = cond_to_ast(s);
                if (res.empty()) res = "VID:" + s;
                return res;
            };

            std::string cond_node = parse_logic_expr(parse_logic_expr, cond);
            if (cond_node.empty()) cond_node = "RAW:" + cond;
            ast_lines.push_back(
                pad + "ID:{" + opcode + " COND:" + cond_node + "}MEID:LID:" + std::to_string(src_line));
            return 'I';
        }
        // ---------- ELSE ----------
        else if (content.rfind("ELSE", 0) == 0) {
            ast_lines.push_back(pad + "ID:{KID:ELSE}MEID:LID:" + std::to_string(src_line));
            return 'I';
        }
        // ---------- BREAK ----------
        else if (content.rfind("BREAK", 0) == 0 &&
                 (content.size() == 5 || content[5] == ' ' || content[5] == '\t')) {
            ast_lines.push_back(pad + "ID:{KID:BREAK}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- CONTINUE ----------
        else if (content.rfind("CONTINUE", 0) == 0 &&
                 (content.size() == 8 || content[8] == ' ' || content[8] == '\t' || content[8] == ':')) {
            ast_lines.push_back(pad + "ID:{KID:CONTINUE}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- DEL ----------
        else if (content.rfind("DEL", 0) == 0 &&
                 (content.size() == 3 || content[3] == ' ' || content[3] == '\t' ||
                  content[3] == '(')) {
            std::string rest = trim(content.substr(3));
            while (!rest.empty() && (rest.back() == ';' || rest.back() == ':' || rest.back() == ' '))
                rest.pop_back();
            if (!rest.empty() && rest.front() == '(' && rest.back() == ')')
                rest = trim(rest.substr(1, rest.size() - 2));
            if (rest.empty()) return 0;
            std::string name_node;
            if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') {
                name_node = "SID:22[" + rest.substr(1, rest.size() - 2) + "]22";
            } else {
                name_node = "VID:" + rest;
            }
            ast_lines.push_back(
                pad + "ID:{KID:DEL NAME:" + name_node + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- PASS ----------
        else if (content.rfind("PASS", 0) == 0 &&
                 (content.size() == 4 || content[4] == ' ' || content[4] == '\t' || content[4] == ':')) {
            ast_lines.push_back(pad + "ID:{KID:PASS}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- RETURN ----------
        else if (content.rfind("RETURN", 0) == 0) {
            if (content.size() == 6 || content[6] == ' ' || content[6] == '\t') {
                std::string rest = content.size() > 6 ? trim(content.substr(6)) : "";
                while (!rest.empty() && (rest.back() == ';' || rest.back() == ' ')) rest.pop_back();

                std::function<std::string(const std::string&)> parse_ret_expr =
                    [&](const std::string& raw) -> std::string {
                    std::string e = trim(raw);
                    if (e.empty()) return "";
                    int depth = 0;
                    bool in_str = false;
                    size_t best_pm = std::string::npos;
                    size_t best_md = std::string::npos;
                    for (size_t k = 0; k < e.size(); ++k) {
                        char c = e[k];
                        if (c == '"') { in_str = !in_str; continue; }
                        if (in_str) continue;
                        if (c == '(') depth++;
                        else if (c == ')') depth--;
                        else if (depth == 0) {
                            if (c == '+' || c == '-') {
                                if (k == 0) continue;
                                char prev = e[k - 1];
                                size_t j = k;
                                while (prev == ' ' && j > 1) prev = e[--j - 1];
                                if (prev == '+' || prev == '-' || prev == '*' ||
                                    prev == '/' || prev == '(' || prev == ',') continue;
                                best_pm = k;
                            } else if ((c == '*' || c == '/') && k > 0) {
                              if (best_md == std::string::npos) best_md = k;
                            }
                        }
                    }
                    size_t op_pos = (best_pm != std::string::npos) ? best_pm : best_md;
                    if (op_pos == std::string::npos) return parse_expression(e);
                    char op = e[op_pos];
                    std::string L = parse_ret_expr(e.substr(0, op_pos));
                    std::string R = parse_ret_expr(e.substr(op_pos + 1));
                    std::string tag;
                    switch (op) {
                        case '+': tag = "[ADD]"; break;
                        case '-': tag = "[SUB]"; break;
                        case '*': tag = "[MUL]"; break;
                        case '/': tag = "[DIV]"; break;
                    }
                    return L + tag + R;
                };

                std::string expr_node = rest.empty() ? "" : parse_ret_expr(rest);
                std::string ret_line = pad + "ID:{KID:RETURN";
                if (!expr_node.empty()) ret_line += " " + expr_node;
                ret_line += "}MEID:LID:" + std::to_string(src_line);
                ast_lines.push_back(ret_line);
                return 0;
            }
        }
        // ---------- BUILD ----------
        else if (content.rfind("BUILD", 0) == 0 && 
                 (content.size() == 5 || content[5] == ' ' || content[5] == '\t' || 
                  content[5] == '\n' || isdigit((unsigned char)content[5]))) {

            std::string rest = trim(content.substr(5));
            std::string as_clause = "";

            std::string force_tag = "";
            size_t t_pos = rest.find(" -t");
            if (t_pos != std::string::npos) {
                force_tag = " -t"; rest.erase(t_pos, 3);
            } else {
                size_t t_pos2 = rest.find("-t ");
                if (t_pos2 != std::string::npos) {
                    force_tag = "-t "; rest.erase(t_pos2, 3);
                }
            }
            std::string upper_rest = rest;
            for (char &c : upper_rest) c = toupper(c);
            size_t as_pos = upper_rest.find(" AS ");
            if (as_pos != std::string::npos) {
                std::string as_var = trim(rest.substr(as_pos + 4));
                rest = trim(rest.substr(0, as_pos));
                as_clause = " AS:{KID:AS VALUE:VID:" + as_var + "}";
            }
            if (rest.empty()) return 0;

            size_t sp = rest.find(' ');
            std::string mode_token = (sp == std::string::npos) ? rest : rest.substr(0, sp);
            std::string mode_part;

            if (mode_token == "b" || mode_token == "o" || mode_token == "d") {
                mode_part = " MODE:SID:22[" + mode_token + "]22";
                rest = (sp == std::string::npos) ? "" : trim(rest.substr(sp + 1));
            } else if (!mode_token.empty() && isalpha((unsigned char)mode_token[0])) {
                mode_part = " MODE:VID:" + mode_token;
                rest = (sp == std::string::npos) ? "" : trim(rest.substr(sp + 1));
            } else {
                mode_part = " MODE:SID:22[d]22";
            }
            if (rest.empty()) return 0;
            std::string value_node = parse_expression(rest);
            if (!force_tag.empty()) value_node += force_tag;

            ast_lines.push_back(
                pad + "ID:{KID:BUILD" + mode_part + " VALUE:" + value_node + as_clause +
                "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- DECOMPILE ----------
        else if ((content.rfind("DECOMPILE", 0) == 0 || content.rfind("decompile", 0) == 0) &&
                 (content.size() == 9 || content[9] == ' ' || content[9] == '\t' || 
                  content[9] == '\n' || isdigit((unsigned char)content[9]) || 
                  isalnum((unsigned char)content[9]) || content[9] == '_')) {
            std::string rest = trim(content.substr(9));
            std::string as_clause = "";
            std::string upper_rest = rest;
            for (char &c : upper_rest) c = toupper(c);
            size_t as_pos = upper_rest.find(" AS ");
            if (as_pos != std::string::npos) {
                std::string as_var = trim(rest.substr(as_pos + 4));
                rest = trim(rest.substr(0, as_pos));
                as_clause = " AS:{KID:AS VALUE:VID:" + as_var + "}";
            }
            if (rest.empty()) return 0;
            bool is_bare_ident = !rest.empty();
            for (char c : rest) {
                if (!isalnum((unsigned char)c) && c != '_') { is_bare_ident = false; break; }
            }
            std::string value_field;
            if (is_bare_ident) {
                value_field = "FILE:VID:" + rest;
            } else {
                if (rest.size() >= 2 &&
                    ((rest.front() == '"' && rest.back() == '"') ||
                     (rest.front() == '\'' && rest.back() == '\''))) {
                    rest = rest.substr(1, rest.size() - 2);
                }
                value_field = "VALUE:SID:22[" + rest + "]22";
            }
            ast_lines.push_back(
                pad + "ID:{KID:DECOMPILE " + value_field + as_clause +
                "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- PRINT ----------
        else if (content.rfind("PRINT", 0) == 0) {
            bool is_print_call = (content.size() > 5) && (content[5] == ' ' || content[5] == '(');
            if (is_print_call) {
                size_t start = content.find('(');
                size_t end = content.rfind(')');
                if (start != std::string::npos && end != std::string::npos && start < end) {
                    std::string inside = trim(content.substr(start + 1, end - start - 1));
                    std::vector<std::string> parts;
                    std::string temp = "";
                    bool in_string = false;
                    int depth = 0;
                    for (char c : inside) {
                        if (c == '"') in_string = !in_string;
                        if (!in_string) {
                            if (c == '(') depth++;
                            else if (c == ')') depth--;
                        }
                        if (c == ',' && !in_string && depth == 0) {
                            if (!temp.empty()) { parts.push_back(trim(temp)); temp = ""; }
                        } else temp += c;
                    }
                    if (!temp.empty()) parts.push_back(trim(temp));

                    std::string print_node_code = "";
                    for (const auto& part : parts) {
                        size_t _lp = part.find('(');
                        bool _is_method = false;

                        if (_lp != std::string::npos) {
                            std::string _fname = trim(part.substr(0, _lp));
                            size_t _dot = _fname.find('.');
                            if (_dot != std::string::npos && _dot > 0 && _dot + 1 < _fname.size()) {
                                std::string _obj = trim(_fname.substr(0, _dot));
                                std::string _m   = trim(_fname.substr(_dot + 1));
                                bool _valid = !_obj.empty() && !_m.empty();
                                for (char c : _obj) if (!isalnum((unsigned char)c) && c != '_') { _valid = false; break; }
                                if (_valid) for (char c : _m) if (!isalnum((unsigned char)c) && c != '_') { _valid = false; break; }
                                if (_valid) {
                                    int _d = 1;
                                    size_t _rp = std::string::npos;
                                    for (size_t k = _lp + 1; k < part.size(); ++k) {
                                        if (part[k] == '(') _d++;
                                        else if (part[k] == ')') { _d--; if (_d == 0) { _rp = k; break; } }
                                    }
                                    if (_rp != std::string::npos) {
                                        std::string _args_str = trim(part.substr(_lp + 1, _rp - _lp - 1));
                                        std::vector<std::string> _args;
                                        if (!_args_str.empty()) {
                                            std::string _t; bool _ins = false; int _dp = 0;
                                            for (char c : _args_str) {
                                               if (c == '"') _ins = !_ins;
                                                if (!_ins && c == '(') _dp++;
                                               else if (!_ins && c == ')') _dp--;
                                                if (!_ins && _dp == 0 && c == ',') {
                                                    if (!_t.empty()) { _args.push_back(trim(_t)); _t.clear(); }
                                                } else _t += c;
                                            }
                                            if (!_t.empty()) _args.push_back(trim(_t));
                                        }
                                        std::string _args_node;
                                        for (size_t k = 0; k < _args.size(); ++k) {
                                            if (k > 0) _args_node += ",";
                                            _args_node += parse_expression(_args[k]);
                                        }
                                        print_node_code += "FID:METHOD OBJ:VID:" + _obj
                                                         + " NAME:SID:22[" + _m + "]22 ARGS:" + _args_node + " ";
                                        _is_method = true;
                                    }
                                }
                            }
                        }
                        if (!_is_method) {
                            if (!part.empty() && part[0] == '"') {
                                size_t close_q = std::string::npos;
                                for (size_t k = 1; k < part.size(); ++k) {
                                    if (part[k] == '\\') { k++; continue; }
                                    if (part[k] == '"') { close_q = k; break; }
                                }
                                if (close_q != std::string::npos &&
                                    close_q + 1 < part.size() &&
                                    part[close_q + 1] == '[') {
                                    size_t close_b = part.find(']', close_q + 2);
                                    if (close_b != std::string::npos) {
                                        std::string sval = part.substr(1, close_q - 1);
                                        std::string idx  = trim(part.substr(close_q + 2,
                                                                            close_b - close_q - 2));
                                        bool is_num = !idx.empty();
                                        for (size_t k = 0; k < idx.size(); ++k) {
                                            if (k == 0 && idx[k] == '-') continue;
                                            if (!isdigit((unsigned char)idx[k])) { is_num = false; break; }
                                        }
                                        if (idx == "-") is_num = false;
                                        if (is_num) {
                                            int idx_val = std::stoi(idx);
                                            int total_chars = 0;
                                            {
                                                int ii = 0;
                                                while (ii < (int)sval.size()) {
                                                    unsigned char ch = sval[ii];
                                                    int len = 1;
                                                    if      ((ch & 0x80) == 0)    len = 1;
                                                    else if ((ch & 0xE0) == 0xC0) len = 2;
                                                    else if ((ch & 0xF0) == 0xE0) len = 3;
                                                    else if ((ch & 0xF8) == 0xF0) len = 4;
                                                    ii += len; total_chars++;
                                                }
                                            }
                                            int real_idx = idx_val;
                                            if (real_idx < 0) real_idx = total_chars + real_idx;
                                            std::string ch_str = "";
                                            if (real_idx >= 0 && real_idx < total_chars) {
                                                int i = 0, cnt = 0;
                                                while (i < (int)sval.size() && cnt < real_idx) {
                                                    unsigned char ch = sval[i];
                                                    int len = 1;
                                                    if      ((ch & 0x80) == 0)    len = 1;
                                                    else if ((ch & 0xE0) == 0xC0) len = 2;
                                                    else if ((ch & 0xF0) == 0xE0) len = 3;
                                                    else if ((ch & 0xF8) == 0xF0) len = 4;
                                                    i += len; cnt++;
                                                }
                                                if (i < (int)sval.size()) {
                                                    unsigned char ch = sval[i];
                                                    int len = 1;
                                                    if      ((ch & 0x80) == 0)    len = 1;
                                                    else if ((ch & 0xE0) == 0xC0) len = 2;
                                                    else if ((ch & 0xF0) == 0xE0) len = 3;
                                                    else if ((ch & 0xF8) == 0xF0) len = 4;
                                                    ch_str = sval.substr(i, len);
                                                }
                                            }
                                            print_node_code += "SID:22[" + ch_str + "]22 ";
                                            continue;
                                        }
                                    }
                                }
                            }
                            size_t or_pos  = part.find(" or ");
                            size_t and_pos = part.find(" and ");
                            if (or_pos != std::string::npos) {
                                std::string left  = trim(part.substr(0, or_pos));
                                std::string right = trim(part.substr(or_pos + 4));
                                print_node_code += parse_expression(left) + "[OR]"
                                                 + parse_expression(right) + " ";
                                continue;
                            }
                            else if (and_pos != std::string::npos) {
                                std::string left  = trim(part.substr(0, and_pos));
                                std::string right = trim(part.substr(and_pos + 5));
                                print_node_code += parse_expression(left) + "[AND]"
                                                 + parse_expression(right) + " ";
                                continue;
                            }
                            else if (part.rfind("not ", 0) == 0 || part.rfind("NOT ", 0) == 0) {
                                std::string operand = trim(part.substr(4));
                                print_node_code += "[NOT]" + parse_expression(operand) + " ";
                                continue;
                            }
                            print_node_code += parse_expression(part) + " ";
                        }
                    }
                    std::string as_part = "";
                    size_t as_pos = content.find(" as ", end);
                    if (as_pos == std::string::npos) as_pos = content.find(" AS ", end);
                    if (as_pos != std::string::npos) {
                        std::string as_target = trim(content.substr(as_pos + 4));
                        std::string as_name;
                        for (char c : as_target) {
                            if (isalnum((unsigned char)c) || c == '_') as_name += c;
                            else break;
                        }
                        if (!as_name.empty())
                            as_part = " KID:AS VALUE:VID:" + as_name + " ";
                    }
                    ast_lines.push_back(
                        pad + "ID:{CHID:{FID:PRINT " + print_node_code + as_part + "}}MEID:LID:" + std::to_string(src_line));
                    return 0;
                }
            }
        }
        // ---------- INPUT 赋值 ----------
        else if (content.find("= input(")  != std::string::npos ||
                 content.find("= INPUT(")  != std::string::npos ||
                 content.find("= int(input(")     != std::string::npos ||
                 content.find("= float(input(")   != std::string::npos ||
                 content.find("= string(input(")  != std::string::npos ||
                 content.find("= int(INPUT(")     != std::string::npos ||
                 content.find("= float(INPUT(")   != std::string::npos ||
                 content.find("= string(INPUT(")  != std::string::npos) {

            size_t eq = content.find('=');
            std::string left  = trim(content.substr(0, eq));
            std::string right = trim(content.substr(eq + 1));
            std::string node = parseValue(right);

            std::string const_flag = is_const_decl ? "CONST:TRUE" : "CONST:FALSE";
            std::string g_flag     = is_global_decl ? " GLOBAL:TRUE" : "";
            std::string t_flag     = is_init ? " INIT:TRUE" : "";
            std::string ty_flag    = var_type.empty() ? "" : " TYPE:" + var_type;

            uint32_t pid = allocate_address(left);
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
            std::string pid_str(pid_buf);
            ast_lines.push_back(
                pad + "ID:{PID:" + pid_str + " {VID:" + left + "} " + const_flag + g_flag + t_flag + ty_flag +
                " [3D] " + node + " }MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- LEN 赋值 ----------
        else if (content.find("= len(") != std::string::npos ||
                 content.find("= LEN(") != std::string::npos) {
            size_t eq = content.find('=');
            std::string left  = trim(content.substr(0, eq));
            std::string right = trim(content.substr(eq + 1));
            size_t lp = right.find('(');
            size_t rp = right.rfind(')');
            std::string var_name;
            if (lp != std::string::npos && rp > lp)
                var_name = trim(right.substr(lp + 1, rp - lp - 1));
            std::string const_flag = is_const_decl ? "CONST:TRUE" : "CONST:FALSE";
            std::string g_flag     = is_global_decl ? " GLOBAL:TRUE" : "";
            uint32_t pid = allocate_address(left);
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
            std::string pid_str(pid_buf);
            ast_lines.push_back(
                pad + "ID:{PID:" + pid_str + "{VID:" + left + "}" + const_flag + g_flag +
                "[3D]FID:LEN VID:" + var_name + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- ABS 赋值 ----------
        else if (content.find("= abs(") != std::string::npos ||
                 content.find("= ABS(") != std::string::npos) {
            size_t eq = content.find('=');
            std::string left  = trim(content.substr(0, eq));
            std::string right = trim(content.substr(eq + 1));
            size_t lp = right.find('(');
            size_t rp = right.rfind(')');
            std::string var_name;
            if (lp != std::string::npos && rp > lp)
                var_name = trim(right.substr(lp + 1, rp - lp - 1));
            std::string const_flag = is_const_decl ? "CONST:TRUE" : "CONST:FALSE";
            std::string g_flag     = is_global_decl ? " GLOBAL:TRUE" : "";
            uint32_t pid = allocate_address(left);
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
            std::string pid_str(pid_buf);
            ast_lines.push_back(
                pad + "ID:{PID:" + pid_str + "{VID:" + left + "}" + const_flag + g_flag +
                "[3D]FID:ABS VID:" + var_name + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- STR 赋值 ----------
        else if (content.find("= str(") != std::string::npos ||
                 content.find("= STR(") != std::string::npos) {
            size_t eq = content.find('=');
            std::string left  = trim(content.substr(0, eq));
            std::string right = trim(content.substr(eq + 1));
            size_t lp = right.find('(');
            size_t rp = right.rfind(')');
            std::string var_name;
            if (lp != std::string::npos && rp > lp)
                var_name = trim(right.substr(lp + 1, rp - lp - 1));
            std::string const_flag = is_const_decl ? "CONST:TRUE" : "CONST:FALSE";
            std::string g_flag     = is_global_decl ? " GLOBAL:TRUE" : "";
            uint32_t pid = allocate_address(left);
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
            std::string pid_str(pid_buf);
            ast_lines.push_back(
                pad + "ID:{PID:" + pid_str + "{VID:" + left + "}" + const_flag + g_flag +
                "[3D]FID:STR VID:" + var_name + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- INT 赋值 ----------
        else if (content.find("= int(") != std::string::npos ||
                 content.find("= INT(") != std::string::npos) {
            size_t eq = content.find('=');
            std::string left  = trim(content.substr(0, eq));
            std::string right = trim(content.substr(eq + 1));
            size_t lp = right.find('(');
            size_t rp = right.rfind(')');
            std::string var_name;
            if (lp != std::string::npos && rp > lp)
                var_name = trim(right.substr(lp + 1, rp - lp - 1));
            std::string const_flag = is_const_decl ? "CONST:TRUE" : "CONST:FALSE";
            std::string g_flag     = is_global_decl ? " GLOBAL:TRUE" : "";
            uint32_t pid = allocate_address(left);
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
            std::string pid_str(pid_buf);
            ast_lines.push_back(
                pad + "ID:{PID:" + pid_str + "{VID:" + left + "}" + const_flag + g_flag +
                "[3D]FID:INT VID:" + var_name + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- BOOL 赋值 ----------
        else if (content.find("= bool(") != std::string::npos ||
                 content.find("= BOOL(") != std::string::npos) {
            size_t eq = content.find('=');
            std::string left  = trim(content.substr(0, eq));
            std::string right = trim(content.substr(eq + 1));
            size_t lp = right.find('(');
            size_t rp = right.rfind(')');
            std::string var_name;
            if (lp != std::string::npos && rp > lp)
                var_name = trim(right.substr(lp + 1, rp - lp - 1));
            std::string const_flag = is_const_decl ? "CONST:TRUE" : "CONST:FALSE";
            std::string g_flag     = is_global_decl ? " GLOBAL:TRUE" : "";
            uint32_t pid = allocate_address(left);
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
            std::string pid_str(pid_buf);
            ast_lines.push_back(
                pad + "ID:{PID:" + pid_str + "{VID:" + left + "}" + const_flag + g_flag +
                "[3D]FID:BOOL VID:" + var_name + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- MAX / MIN 赋值 ----------
        else if (content.find("= max(") != std::string::npos ||
                 content.find("= MAX(") != std::string::npos ||
                 content.find("= min(") != std::string::npos ||
                 content.find("= MIN(") != std::string::npos) {
           size_t eq = content.find('=');
            std::string left  = trim(content.substr(0, eq));
            std::string right = trim(content.substr(eq + 1));
            bool is_max = (right.rfind("max(", 0) == 0 || right.rfind("MAX(", 0) == 0);
            std::string opcode = is_max ? "FID:MAX" : "FID:MIN";
            size_t lp = right.find('(');
            size_t rp = right.rfind(')');
            std::string a, b;
            if (lp != std::string::npos && rp > lp) {
                std::string inside = trim(right.substr(lp + 1, rp - lp - 1));
                size_t comma = inside.find(',');
                if (comma != std::string::npos) {
                    a = trim(inside.substr(0, comma));
                    b = trim(inside.substr(comma + 1));
                }
            }
            std::string const_flag = is_const_decl ? "CONST:TRUE" : "CONST:FALSE";
            std::string g_flag     = is_global_decl ? " GLOBAL:TRUE" : "";
            uint32_t pid = allocate_address(left);
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
            std::string pid_str(pid_buf);
            ast_lines.push_back(
                pad + "ID:{PID:" + pid_str +"{VID:" + left + "}" + const_flag + g_flag +
                "[3D]" + opcode + " VID:" + a + " VID:" + b + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- HEX 赋值 ----------
        else if (content.find("= hex(") != std::string::npos ||
                 content.find("= HEX(") != std::string::npos) {
            size_t eq = content.find('=');
            std::string left  = trim(content.substr(0, eq));
            std::string right = trim(content.substr(eq + 1));
            size_t lp = right.find('(');
            size_t rp = right.rfind(')');
            std::string var_name;
            if (lp != std::string::npos && rp > lp)
                var_name = trim(right.substr(lp + 1, rp - lp - 1));
            std::string const_flag = is_const_decl ? "CONST:TRUE" : "CONST:FALSE";
            std::string g_flag     = is_global_decl ? " GLOBAL:TRUE" : "";
            uint32_t pid = allocate_address(left);
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
            std::string pid_str(pid_buf);
            ast_lines.push_back(
                pad + "ID:{PID:" + pid_str +"{VID:" + left + "}" + const_flag + g_flag +
                "[3D]FID:HEX VID:" + var_name + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- BIN / OCT 赋值 ----------
        else if (content.find("= bin(") != std::string::npos ||
                 content.find("= BIN(") != std::string::npos ||
                 content.find("= oct(") != std::string::npos ||
                 content.find("= OCT(") != std::string::npos) {
            size_t eq = content.find('=');
            std::string left  = trim(content.substr(0, eq));
            std::string right = trim(content.substr(eq + 1));
            bool is_bin = (right.rfind("bin(", 0) == 0 || right.rfind("BIN(", 0) == 0);
            std::string opcode = is_bin ? "FID:BIN" : "FID:OCT";
            size_t lp = right.find('(');
            size_t rp = right.rfind(')');
            std::string var_name;
            if (lp != std::string::npos && rp > lp)
                var_name = trim(right.substr(lp + 1, rp - lp - 1));
            std::string const_flag = is_const_decl ? "CONST:TRUE" : "CONST:FALSE";
            std::string g_flag     = is_global_decl ? " GLOBAL:TRUE" : "";
            uint32_t pid = allocate_address(left);
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
            std::string pid_str(pid_buf);
            ast_lines.push_back(
                pad + "ID:{PID:" + pid_str + "{VID:" + left + "}" + const_flag + g_flag +
                "[3D]" + opcode + " VID:" + var_name + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- CHR / ORD 赋值 ----------
        else if (content.find("= chr(") != std::string::npos ||
                 content.find("= CHR(") != std::string::npos ||
                 content.find("= ord(") != std::string::npos ||
                 content.find("= ORD(") != std::string::npos) {
            size_t eq = content.find('=');
            std::string left  = trim(content.substr(0, eq));
            std::string right = trim(content.substr(eq + 1));
            bool is_chr = (right.rfind("chr(", 0) == 0 || right.rfind("CHR(", 0) == 0);
            std::string opcode = is_chr ? "FID:CHR" : "FID:ORD";
            size_t lp = right.find('(');
            size_t rp = right.rfind(')');
            std::string arg;
            if (lp != std::string::npos && rp > lp)
                arg = trim(right.substr(lp + 1, rp - lp - 1));
            std::string field = "VID:" + arg;
            uint32_t pid = allocate_address(left);
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
            std::string pid_str(pid_buf);
            if (!is_chr && arg.size() >= 2 &&
                (arg.front() == '"' || arg.front() == '\'')) {
                std::string v = arg.substr(1, arg.size() - 2);
                int code = v.empty() ? 0 : (unsigned char)v[0];
                ast_lines.push_back(
                    pad + "ID:{PID:" + pid_str + "{VID:" + left + "}CONST:FALSE[3D]NID:" +
                    std::to_string(code) + "}MEID:LID:" + std::to_string(src_line));
                return 0;
            }
            std::string const_flag = is_const_decl ? "CONST:TRUE" : "CONST:FALSE";
            std::string g_flag     = is_global_decl ? " GLOBAL:TRUE" : "";
            ast_lines.push_back(
                pad + "ID:{PID:" + pid_str + "{VID:" + left + "}" + const_flag + g_flag +
                "[3D]" + opcode + " " + field + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- ROUND / FLOOR / CEIL 赋值 ----------
        else if (content.find("= round(") != std::string::npos ||
                 content.find("= ROUND(") != std::string::npos ||
                 content.find("= floor(") != std::string::npos ||
                 content.find("= FLOOR(") != std::string::npos ||
                 content.find("= ceil(")  != std::string::npos ||
                 content.find("= CEIL(")  != std::string::npos) {
            size_t eq = content.find('=');
            std::string left  = trim(content.substr(0, eq));
            std::string right = trim(content.substr(eq + 1));
            std::string opcode;
            if (right.rfind("round(", 0) == 0 || right.rfind("ROUND(", 0) == 0) opcode = "FID:ROUND";
            else if (right.rfind("floor(", 0) == 0 || right.rfind("FLOOR(", 0) == 0) opcode = "FID:FLOOR";
            else opcode = "FID:CEIL";
            size_t lp = right.find('(');
            size_t rp = right.rfind(')');
            std::string var_name;
            if (lp != std::string::npos && rp > lp)
                var_name = trim(right.substr(lp + 1, rp - lp - 1));
            std::string const_flag = is_const_decl ? "CONST:TRUE" : "CONST:FALSE";
            std::string g_flag     = is_global_decl ? " GLOBAL:TRUE" : "";
            uint32_t pid = allocate_address(left);
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
            std::string pid_str(pid_buf);
            ast_lines.push_back(
               pad + "ID:{PID:" + pid_str +"{VID:" + left + "}" + const_flag + g_flag +
               "[3D]" + opcode + " VID:" + var_name + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- UPPER / LOWER / REVERSE 赋值 ----------
        else if (content.find("= upper(")   != std::string::npos ||
                 content.find("= UPPER(")   != std::string::npos ||
                 content.find("= lower(")   != std::string::npos ||
                 content.find("= LOWER(")   != std::string::npos ||
                 content.find("= reverse(") != std::string::npos ||
                 content.find("= REVERSE(") != std::string::npos) {
            size_t eq = content.find('=');
            std::string left  = trim(content.substr(0, eq));
            std::string right = trim(content.substr(eq + 1));
            std::string opcode;
            if      (right.rfind("upper(", 0) == 0 || right.rfind("UPPER(", 0) == 0) opcode = "FID:UPPER";
            else if (right.rfind("lower(", 0) == 0 || right.rfind("LOWER(", 0) == 0) opcode = "FID:LOWER";
            else                                                                      opcode = "FID:REVERSE";
            size_t lp = right.find('(');
            size_t rp = right.rfind(')');
            std::string var_name;
            if (lp != std::string::npos && rp > lp)
                var_name = trim(right.substr(lp + 1, rp - lp - 1));
            std::string const_flag = is_const_decl ? "CONST:TRUE" : "CONST:FALSE";
            std::string g_flag     = is_global_decl ? " GLOBAL:TRUE" : "";
            uint32_t pid = allocate_address(left);
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
            std::string pid_str(pid_buf);
            ast_lines.push_back(
                pad + "ID:{PID:" + pid_str + "{VID:" + left + "}" + const_flag + g_flag +
                "[3D]" + opcode + " VID:" + var_name + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- ASCII / HASH / ID / REPR / HELP 赋值 ----------
        else if (content.find("= ascii(") != std::string::npos ||
                 content.find("= ASCII(") != std::string::npos ||
                 content.find("= hash(")  != std::string::npos ||
                 content.find("= HASH(")  != std::string::npos ||
                 content.find("= id(")    != std::string::npos ||
                 content.find("= ID(")    != std::string::npos ||
                 content.find("= repr(")  != std::string::npos ||
                 content.find("= REPR(")  != std::string::npos ||
                 content.find("= help(")  != std::string::npos ||
                 content.find("= HELP(")  != std::string::npos) {
            size_t eq = content.find('=');
            std::string left  = trim(content.substr(0, eq));
            std::string right = trim(content.substr(eq + 1));
            std::string opcode;
            if      (right.rfind("ascii(", 0) == 0 || right.rfind("ASCII(", 0) == 0) opcode = "FID:ASCII";
            else if (right.rfind("hash(",  0) == 0 || right.rfind("HASH(",  0) == 0) opcode = "FID:HASH";
            else if (right.rfind("id(",    0) == 0 || right.rfind("ID(",    0) == 0) opcode = "FID:ID";
            else if (right.rfind("repr(",  0) == 0 || right.rfind("REPR(",  0) == 0) opcode = "FID:REPR";
            else                                                                      opcode = "FID:HELP";
            size_t lp = right.find('(');
            size_t rp = right.rfind(')');
            std::string var_name;
            if (lp != std::string::npos && rp > lp)
                var_name = trim(right.substr(lp + 1, rp - lp - 1));
            std::string const_flag = is_const_decl ? "CONST:TRUE" : "CONST:FALSE";
            std::string g_flag     = is_global_decl ? " GLOBAL:TRUE" : "";
            uint32_t pid = allocate_address(left);
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
            std::string pid_str(pid_buf);
            ast_lines.push_back(
                pad + "ID:{PID:" + pid_str +"{VID:" + left + "}" + const_flag + g_flag +
                "[3D]" + opcode + " VID:" + var_name + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- POW / SUM 赋值（双参数） ----------
        else if (content.find("= pow(") != std::string::npos ||
                 content.find("= POW(") != std::string::npos ||
                 content.find("= sum(") != std::string::npos ||
                 content.find("= SUM(") != std::string::npos) {
            size_t eq = content.find('=');
            std::string left  = trim(content.substr(0, eq));
            std::string right = trim(content.substr(eq + 1));
            std::string opcode;
            if (right.rfind("pow(", 0) == 0 || right.rfind("POW(", 0) == 0) opcode = "FID:POW";
            else                                                              opcode = "FID:SUM";
            size_t lp = right.find('(');
            size_t rp = right.rfind(')');
            std::string a, b;
            if (lp != std::string::npos && rp > lp) {
                std::string inside = trim(right.substr(lp + 1, rp - lp - 1));
                size_t comma = inside.find(',');
                if (comma != std::string::npos) {
                    a = trim(inside.substr(0, comma));
                    b = trim(inside.substr(comma + 1));
                }
            }
            std::string const_flag = is_const_decl ? "CONST:TRUE" : "CONST:FALSE";
            std::string g_flag     = is_global_decl ? " GLOBAL:TRUE" : "";
            uint32_t pid = allocate_address(left);
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
            std::string pid_str(pid_buf);
            ast_lines.push_back(
                pad + "ID:{PID:" + pid_str + "{VID:" + left + "}" + const_flag + g_flag +
                "[3D]" + opcode + " VID:" + a + " VID:" + b + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- STRIP / FIND / REPLACE 赋值 ----------
        else if (content.find("= strip(")   != std::string::npos ||
                 content.find("= STRIP(")   != std::string::npos ||
                 content.find("= find(")    != std::string::npos ||
                 content.find("= FIND(")    != std::string::npos ||
                 content.find("= replace(") != std::string::npos ||
                 content.find("= REPLACE(") != std::string::npos) {
            size_t eq = content.find('=');
            std::string left  = trim(content.substr(0, eq));
            std::string right = trim(content.substr(eq + 1));
            std::string opcode;
            if      (right.rfind("strip(", 0) == 0 || right.rfind("STRIP(", 0) == 0) opcode = "FID:STRIP";
            else if (right.rfind("find(",  0) == 0 || right.rfind("FIND(",  0) == 0) opcode = "FID:FIND";
            else                                                                      opcode = "FID:REPLACE";
            size_t lp = right.find('(');
            size_t rp = right.rfind(')');
            std::string args_part;
            if (lp != std::string::npos && rp > lp)
                args_part = trim(right.substr(lp + 1, rp - lp - 1));
            std::vector<std::string> args;
            std::string cur;
            bool in_str = false;
            for (char c : args_part) {
                if (c == '"') in_str = !in_str;
                if (c == ',' && !in_str) { args.push_back(trim(cur)); cur.clear(); }
                else cur += c;
            }
            if (!cur.empty()) args.push_back(trim(cur));
            std::string args_field;
            for (const auto& a : args) args_field += " VID:" + a;
            std::string const_flag = is_const_decl ? "CONST:TRUE" : "CONST:FALSE";
            std::string g_flag     = is_global_decl ? " GLOBAL:TRUE" : "";
            uint32_t pid = allocate_address(left);
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
            std::string pid_str(pid_buf);
            ast_lines.push_back(
                pad + "ID:{PID:" + pid_str + "{VID:" + left + "}" + const_flag + g_flag +
                "[3D]" + opcode + args_field + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- WITH OPEN ----------
        else if (content.rfind("WITH ", 0) == 0 || content.rfind("with ", 0) == 0) {
            size_t lp = content.find('(');
            size_t rp = content.rfind(')');
            if (lp == std::string::npos || rp <= lp) return 0;
            std::string inside = trim(content.substr(lp + 1, rp - lp - 1));
            std::vector<std::string> parts;
            std::string tmp;
            bool in_str = false;
            for (char c : inside) {
                if (c == '"') in_str = !in_str;
                if (c == ',' && !in_str) {
                    if (!tmp.empty()) { parts.push_back(trim(tmp)); tmp.clear(); }
                } else tmp += c;
            }
            if (!tmp.empty()) parts.push_back(trim(tmp));
            if (parts.size() < 2) return 0;

            std::string mode_str = parts[0];
            std::string mode_field;
            if (mode_str.size() >= 2 && mode_str.front() == '"' && mode_str.back() == '"') {
                mode_field = "MODE:SID:22[" + mode_str.substr(1, mode_str.size() - 2) + "]22";
            } else {
                mode_field = "MODE:VID:" + mode_str;
            }
            std::string path_str = parts[1];
            std::string path_field;
            if (path_str.size() >= 2 && path_str.front() == '"' && path_str.back() == '"') {
                path_field = "PATH:SID:22[" + path_str.substr(1, path_str.size() - 2) + "]22";
            } else {
                path_field = "PATH:VID:" + path_str;
            }
            ast_lines.push_back(
                pad + "ID:{KID:WITH_OPEN " + mode_field + " " + path_field +
                "}MEID:LID:" + std::to_string(src_line));
            return 'O';
        }
        // ---------- OPEN ----------
        else if (content.rfind("OPEN(", 0) == 0 || content.rfind("open(", 0) == 0 ||
                 content.find("= OPEN(") != std::string::npos || content.find("=OPEN(")  != std::string::npos ||
                 content.find("= open(") != std::string::npos || content.find("=open(")  != std::string::npos) {
            std::string lhs = "";
            std::string as_var = "";
            std::string call_part = content;
            size_t eq = content.find("= OPEN(");
            if (eq == std::string::npos) eq = content.find("= open(");
            if (eq == std::string::npos) eq = content.find("=OPEN(");
            if (eq == std::string::npos) eq = content.find("=open(");
            if (eq != std::string::npos) {
                lhs = trim(content.substr(0, eq));
                call_part = trim(content.substr(eq + 1));
            }
            size_t as_pos = content.rfind(") as ");
            if (as_pos == std::string::npos) as_pos = content.rfind(")as ");
            if (as_pos == std::string::npos) as_pos = content.rfind(") AS ");
            if (as_pos == std::string::npos) as_pos = content.rfind(")AS ");
            if (as_pos != std::string::npos) {
                std::string v = trim(content.substr(as_pos + 5));
                if (!v.empty()) as_var = v;
                call_part = trim(content.substr(0, as_pos + 1));
            }
            std::string args_str = "";
            size_t lp = call_part.find('(');
            if (lp != std::string::npos) {
                int d = 1;
                size_t rp = std::string::npos;
                for (size_t k = lp + 1; k < call_part.size(); ++k) {
                    if (call_part[k] == '(') d++;
                    else if (call_part[k] == ')') { d--; if (d == 0) { rp = k; break; } }
                }
                if (rp != std::string::npos)
                    args_str = trim(call_part.substr(lp + 1, rp - lp - 1));
            }
            std::vector<std::string> parts;
            if (!args_str.empty()) {
                std::string tmp; bool in_str = false; int dep = 0;
                for (char c : args_str) {
                    if (c == '"') in_str = !in_str;
                    if (!in_str && c == '(') dep++;
                    else if (!in_str && c == ')') dep--;
                    if (!in_str && dep == 0 && c == ',') {
                        if (!tmp.empty()) { parts.push_back(trim(tmp)); tmp.clear(); }
                    } else tmp += c;
                }
                if (!tmp.empty()) parts.push_back(trim(tmp));
            }
            if (parts.empty()) return 0;
            if (parts.size() < 2 || parts.size() > 3) {
                fprintf(stderr, "错误: open 需要 2 或 3 个参数\n");
                return 0;
            }
            auto to_node = [](const std::string& s) -> std::string {
                if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                    return "SID:22[" + s.substr(1, s.size() - 2) + "]22";
                return "VID:" + s;
            };
            std::string node = "FID:OPEN";
            node += " MODE:"   + to_node(parts[0]);
            node += " TARGET:" + to_node(parts[1]);
            if (parts.size() >= 3)
                node += " WRITE:" + to_node(parts[2]);
            if (!as_var.empty()) {
                node += " AS:{KID:AS VALUE:VID:" + as_var + "}";
                ast_lines.push_back(
                    pad + "ID:{" + node + "}MEID:LID:" + std::to_string(src_line));
            }
            else if (!lhs.empty()) {
                uint32_t pid = allocate_address(lhs);
                char pid_buf[16];
                snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
                std::string pid_str(pid_buf);
                ast_lines.push_back(
                    pad + "ID:{PID:" + pid_str + "{VID:" + lhs + "}CONST:FALSE[3D]" + node +
                    "}MEID:LID:" + std::to_string(src_line));
            }
            else {
                ast_lines.push_back(
                    pad + "ID:{" + node + "}MEID:LID:" + std::to_string(src_line));
            }
            return 0;
        }
        // ---------- XYCREATE ----------
        else if (content.rfind("XYCREATE(", 0) == 0 || content.rfind("xycreate(", 0) == 0) {
            size_t lp = content.find('(');
            size_t rp = content.rfind(')');
            if (lp == std::string::npos || rp <= lp) return 0;
            std::string inside = trim(content.substr(lp + 1, rp - lp - 1));
            std::vector<std::string> parts;
            std::string tmp;
            bool in_str = false;
            for (char c : inside) {
                if (c == '"') in_str = !in_str;
                if (c == ',' && !in_str) {
                    if (!tmp.empty()) { parts.push_back(trim(tmp)); tmp.clear(); }
                } else tmp += c;
            }
            if (!tmp.empty()) parts.push_back(trim(tmp));
            if (parts.size() < 5) {
                std::cerr << "[XYCREATE] 参数不足，需要 5 个\n";
                return 0;
            }
            std::string args_field;
            for (size_t k = 0; k < parts.size(); ++k) {
                std::string p = parts[k];
                if (p.size() >= 2 && p.front() == '"' && p.back() == '"') {
                    args_field += "SID:22[" + p.substr(1, p.size() - 2) + "]22";
                } else {
                    args_field += "VID:" + p;
                }
                if (k < parts.size() - 1) args_field += " ";
            }
            ast_lines.push_back(
                pad + "ID:{FID:XYCREATE " + args_field + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- STRCAT ----------
        else if (content.find("STRCAT(") != std::string::npos || 
                 content.find("strcat(") != std::string::npos) {
            std::string as_var = "";
            std::string call_part = content;
            size_t eq = content.find('=');
            if (eq != std::string::npos && 
                (eq + 1 >= content.size() || content[eq + 1] != '=') &&
                (eq == 0 || content[eq - 1] != '=')) {
                as_var = trim(content.substr(0, eq));
                call_part = trim(content.substr(eq + 1));
            }
            size_t lp = call_part.find('(');
            size_t rp = call_part.rfind(')');
            if (lp == std::string::npos || rp <= lp) return 0;
            std::string inside = trim(call_part.substr(lp + 1, rp - lp - 1));
            std::vector<std::string> parts;
            std::string tmp;
            bool in_str = false;
            for (char c : inside) {
                if (c == '"') in_str = !in_str;
                if (c == ',' && !in_str) {
                    if (!tmp.empty()) { parts.push_back(trim(tmp)); tmp.clear(); }
                } else tmp += c;
            }
            if (!tmp.empty()) parts.push_back(trim(tmp));
            if (parts.empty()) return 0;
            std::string args_field;
            for (size_t i = 0; i < parts.size(); ++i) {
                std::string p = parts[i];
                if (p.size() >= 2 && p.front() == '"' && p.back() == '"') {
                    args_field += "SID:22[" + p.substr(1, p.size() - 2) + "]22";
                } else {
                    args_field += "VID:" + p;
                }
                if (i < parts.size() - 1) args_field += " ";
            }
            std::string node;
            if (!as_var.empty()) {
                uint32_t pid = allocate_address(as_var);
                char pid_buf[16];
                snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
                std::string pid_str(pid_buf);
                node = "ID:{PID:" + pid_str + "{VID:" + as_var + 
                       "}CONST:FALSE[3D]FID:STRCAT " + args_field + 
                       "}MEID:LID:" + std::to_string(src_line);
            } else {
                node = "ID:{FID:STRCAT " + args_field + 
                       "}MEID:LID:" + std::to_string(src_line);
            }
            ast_lines.push_back(pad + node);
            return 0;
        }
        // ---------- EXECVM ----------
        else if (content.rfind("EXECVM(", 0) == 0 || content.rfind("execvm(", 0) == 0) {
            size_t lp = content.find('(');
            size_t rp = content.rfind(')');
            if (lp == std::string::npos || rp <= lp) return 0;
            std::string inside = trim(content.substr(lp + 1, rp - lp - 1));
            if (inside.empty()) return 0;
            ast_lines.push_back(
                pad + "ID:{FID:EXECVM VID:" + inside + "}MEID:LID:" + std::to_string(src_line));
            return 0;
        }
        // ---------- BIN7_DECODE ----------
        else if (content.rfind("BIN7_DECODE(", 0) == 0 || content.rfind("bin7_decode(", 0) == 0 ||
                 content.find("= BIN7_DECODE(") != std::string::npos || 
                 content.find("=BIN7_DECODE(") != std::string::npos) {
            std::string as_var = "";
            std::string call_part = content;
            size_t eq = content.find("= BIN7_DECODE(");
            if (eq == std::string::npos) eq = content.find("=BIN7_DECODE(");
            if (eq != std::string::npos) {
                as_var = trim(content.substr(0, eq));
                call_part = trim(content.substr(eq + 1));
            }
            size_t lp = call_part.find('(');
            size_t rp = call_part.rfind(')');
            if (lp == std::string::npos || rp <= lp) return 0;
            std::string inside = trim(call_part.substr(lp + 1, rp - lp - 1));
            std::vector<std::string> parts;
            std::string tmp;
            bool in_str = false;
            for (char c : inside) {
                if (c == '"') in_str = !in_str;
                if (c == ',' && !in_str) {
                    if (!tmp.empty()) { parts.push_back(trim(tmp)); tmp.clear(); }
                } else tmp += c;
            }
            if (!tmp.empty()) parts.push_back(trim(tmp));
            if (parts.size() < 2) return 0;
            std::string data_field;
            std::string data_str = parts[0];
            if (data_str.size() >= 2 && data_str.front() == '"' && data_str.back() == '"') {
                data_field = "SID:22[" + data_str.substr(1, data_str.size() - 2) + "]22";
            } else {
                data_field = "VID:" + data_str;
            }
            std::string bits_field = "BITS:" + parts[1];
            std::string node;
            if (!as_var.empty()) {
                uint32_t pid = allocate_address(as_var);
                char pid_buf[16];
                snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
                std::string pid_str(pid_buf);
                node = "ID:{PID:" + pid_str + "{VID:" + as_var + 
                       "}CONST:FALSE[3D]FID:BIN7_DECODE " + data_field + 
                       " " + bits_field + "}MEID:LID:" + std::to_string(src_line);
            } else {
                node = "ID:{FID:BIN7_DECODE " + data_field + 
                       " " + bits_field + "}MEID:LID:" + std::to_string(src_line);
            }
            ast_lines.push_back(pad + node);
            return 0;
        }
        // ---------- BIN7 ----------
        else if (content.rfind("BIN7(", 0) == 0 || content.rfind("BIN7 ", 0) == 0 ||
                 content.find("= BIN7(") != std::string::npos || 
                 content.find("=BIN7(") != std::string::npos) {
            std::string as_var = "";
            std::string call_part = content;
            size_t eq = content.find("= BIN7(");
            if (eq == std::string::npos) eq = content.find("=BIN7(");
            if (eq != std::string::npos) {
                as_var = trim(content.substr(0, eq));
                call_part = trim(content.substr(eq + 1));
            }
            std::string arg_str = "";
            size_t lp = call_part.find('(');
            size_t rp = call_part.rfind(')');
            if (lp != std::string::npos && rp > lp) {
                arg_str = trim(call_part.substr(lp + 1, rp - lp - 1));
            } else {
                size_t pos = call_part.find("BIN7");
                if (pos != std::string::npos) arg_str = trim(call_part.substr(pos + 4));
            }
            if (arg_str.empty()) return 0;
            std::string arg_field;
            if (arg_str.size() >= 2 && arg_str.front() == '"' && arg_str.back() == '"') {
                arg_field = "SID:22[" + arg_str.substr(1, arg_str.size() - 2) + "]22";
            } else {
                arg_field = "VID:" + arg_str;
            }
            std::string node;
            if (!as_var.empty()) {
                uint32_t pid = allocate_address(as_var);
                char pid_buf[16];
                snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
                std::string pid_str(pid_buf);
                node = "ID:{PID:" + pid_str + "{VID:" + as_var + 
                       "}CONST:FALSE[3D]FID:BIN7 " + arg_field + 
                       "}MEID:LID:" + std::to_string(src_line);
            } else {
                node = "ID:{FID:BIN7 " + arg_field + 
                       "}MEID:LID:" + std::to_string(src_line);
            }
            ast_lines.push_back(pad + node);
            return 0;
        }
        // ---------- 裸函数调用 ----------
        {
            size_t lp = content.find('(');
            size_t rp = content.rfind(')');
            if (lp != std::string::npos && rp == content.size() - 1 && lp < rp && lp > 0) {
                std::string fname = trim(content.substr(0, lp));
                bool is_ident = !fname.empty();
                for (char c : fname) {
                    if (!isalnum((unsigned char)c) && c != '_' && c != '.') {
                        is_ident = false;
                        break;
                    }
                }
                if (is_ident) {
                    std::string args_str = trim(content.substr(lp + 1, rp - lp - 1));
                    std::vector<std::string> arg_names;
                    if (!args_str.empty()) {
                        std::string tmp;
                        bool in_str = false;
                        for (char c : args_str) {
                            if (c == '"') in_str = !in_str;
                            if (c == ',' && !in_str) {
                                if (!tmp.empty()) { arg_names.push_back(trim(tmp)); tmp.clear(); }
                            } else tmp += c;
                        }
                        if (!tmp.empty()) arg_names.push_back(trim(tmp));
                    }
                    std::string args_node;
                    for (size_t k = 0; k < arg_names.size(); ++k) {
                        if (k > 0) args_node += ",";
                        const std::string& a = arg_names[k];
                        if (a.empty()) continue;
                        bool is_num = true;
                        for (size_t j = 0; j < a.size(); ++j) {
                            char c = a[j];
                            if (!isdigit((unsigned char)c) && c != '.' && c != '-' && c != '+') {
                                is_num = false;
                                break;
                            }
                        }
                        if (is_num && !a.empty()) {
                            args_node += (a.find('.') == std::string::npos) ? ("NID:" + a) : ("FLT:" + a);
                        } else if (a.size() >= 2 && a.front() == '"' && a.back() == '"') {
                            args_node += "SID:22[" + a.substr(1, a.size() - 2) + "]22";
                        } else {
                            args_node += "VID:" + a;
                        }
                    }
                    ast_lines.push_back(
                        pad + "ID:{FID:CALL NAME:SID:22[" + fname + "]22 ARGS:" + args_node +
                        "}MEID:LID:" + std::to_string(src_line));
                    return 0;
                }
            }
        }
        // ---------- AWAIT ----------
        {
            size_t await_pos = std::string::npos;
            size_t await_len = 0;
            {
                size_t up = content.find("AWAIT ");
                size_t lo = content.find("await ");
                if (up != std::string::npos && (lo == std::string::npos || up < lo)) {
                    await_pos = up; await_len = 6;
                } else if (lo != std::string::npos) {
                    await_pos = lo; await_len = 6;
                }
            }
            if (await_pos != std::string::npos) {
                std::string left_part = trim(content.substr(0, await_pos));
                std::string rest = trim(content.substr(await_pos + await_len));
                if (!left_part.empty() && left_part.back() == '=') {
                    left_part.pop_back();
                    left_part = trim(left_part);
                } else if (!left_part.empty()) {
                    left_part = "";
                }
                size_t lp = rest.find('(');
                size_t rp = rest.rfind(')');
                if (lp != std::string::npos && rp != std::string::npos && rp == rest.size() - 1 && lp < rp) {
                    std::string fname = trim(rest.substr(0, lp));
                    std::string args_str = trim(rest.substr(lp + 1, rp - lp - 1));
                    std::vector<std::string> arg_names;
                    if (!args_str.empty()) {
                        std::string tmp;
                        bool in_str = false;
                        for (char c : args_str) {
                            if (c == '"') in_str = !in_str;
                            if (c == ',' && !in_str) {
                                if (!tmp.empty()) { arg_names.push_back(trim(tmp)); tmp.clear(); }
                            } else tmp += c;
                        }
                        if (!tmp.empty()) arg_names.push_back(trim(tmp));
                    }
                    std::string mode_flag = "AWAIT:FALSE";
                    if (!arg_names.empty() && arg_names[0].size() >= 2 && 
                        arg_names[0].front() == '"' && arg_names[0].back() == '"') {
                        std::string flag = arg_names[0].substr(1, arg_names[0].size() - 2);
                        if (flag == "await") mode_flag = "AWAIT:TRUE";
                    }
                    std::string slep_flag = "SLEEP:FALSE";
                    if (arg_names.size() >= 3) {
                        std::string third = arg_names[2];
                        if (third == "\"true\"" || third == "\"TRUE\"" || third == "true")
                            slep_flag = "SLEEP:TRUE";
                    }
                    std::string args_node;
                    for (size_t k = 0; k < arg_names.size(); ++k) {
                        if (k > 0) args_node += ",";
                        const std::string& a = arg_names[k];
                        if (a.empty()) continue;
                        bool is_num = true;
                        for (size_t j = 0; j < a.size(); ++j) {
                            char c = a[j];
                            if (!isdigit((unsigned char)c) && c != '.' && c != '-' && c != '+') {
                                is_num = false;
                                break;
                            }
                        }
                        if (is_num && !a.empty()) {
                            if (a.find('.') == std::string::npos) args_node += "NID:" + a;
                            else args_node += "FLT:" + a;
                        } else if (a.size() >= 2 && a.front() == '"' && a.back() == '"') {
                            args_node += "SID:22[" + a.substr(1, a.size() - 2) + "]22";
                        } else {
                            args_node += "VID:" + a;
                        }
                    }
                    std::string await_inner = "KID:AWAIT " + mode_flag + " " + slep_flag;
                    await_inner += " NAME:SID:22[" + fname + "]22 ARGS:" + args_node;
                    if (!left_part.empty()) {
                        ast_lines.push_back(
                            pad + "ID:{PID:0x18{VID:" + left_part + "}CONST:FALSE[3D]" + await_inner + "}MEID:LID:" + std::to_string(src_line));
                    } else {
                        ast_lines.push_back(
                            pad + "ID:{" + await_inner + "}MEID:LID:" + std::to_string(src_line));
                    }
                    return 0;
                }
            }
        }
        // ---------- 复合赋值 += -= *= /= ----------
        {
            size_t eq_pos = content.find('=');
            if (eq_pos != std::string::npos && eq_pos > 0) {
                char prev = content[eq_pos - 1];
                char next = (eq_pos + 1 < content.size()) ? content[eq_pos + 1] : '\0';
                bool is_cmp = (prev == '=' || prev == '>' || prev == '<' || prev == '!') || (next == '=' || next == '>');
                if (!is_cmp && (prev == '+' || prev == '-' || prev == '*' || prev == '/')) {
                    std::string left  = trim(content.substr(0, eq_pos - 1));
                    std::string right = trim(content.substr(eq_pos + 1));
                    if (!left.empty() && !right.empty()) {
                        std::string op_tag;
                        if      (prev == '+') op_tag = "[ADD]";
                        else if (prev == '-') op_tag = "[SUB]";
                        else if (prev == '*') op_tag = "[MUL]";
                        else                  op_tag = "[DIV]";
                        std::string right_node = parse_expression(right);
                        std::string const_flag = is_const_decl ? "CONST:TRUE" : "CONST:FALSE";
                        std::string g_flag = is_global_decl ? " GLOBAL:TRUE" : "";
                        uint32_t pid = allocate_address(left);
                        char pid_buf[16];
                        snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
                        std::string pid_str(pid_buf);
                        ast_lines.push_back(
                            pad + "ID:{PID:" + pid_str + "{VID:" + left + "}" + const_flag + "PRIMARY:TRUE" + g_flag +
                            "[3D]VID:" + left + op_tag + right_node + "}MEID:LID:" + std::to_string(src_line));
                        return 0;
                    }
                }
            }
        }
        // ---------- 函数调用 / 二元表达式 ----------
        {
            std::string left_part;
            std::string call_part = content;
            size_t eq_pos = content.find('=');
            if (eq_pos != std::string::npos &&
                (eq_pos + 1 >= content.size() || content[eq_pos + 1] != '=') &&
                (eq_pos == 0 || content[eq_pos - 1] != '=')) {
                left_part = trim_str(content.substr(0, eq_pos));
                call_part = trim_str(content.substr(eq_pos + 1));
            }
            if (!left_part.empty() && call_part.rfind("trf ", 0) == 0) {
                std::string expr_body = trim_str(call_part.substr(4));
                std::string trf_ast = generate_trf_core_ast(expr_body);
                if (trf_ast.rfind("ERROR:", 0) == 0) {
                    std::cerr << "编译错误 [行 " << src_line << "]: " << trf_ast << std::endl;
                } else {
                    uint32_t pid = allocate_address(left_part);
                    char pid_buf[16];
                    snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
                    std::string pid_str(pid_buf);
                    std::string ast_node = "ID:{PID:" + pid_str;
                    ast_node += " {VID:" + left_part + "}";
                    ast_node += " " + std::string(is_const_decl ? "CONST:TRUE" : "CONST:FALSE");
                    if (is_global_decl) ast_node += " GLOBAL:TRUE";
                    if (is_init) ast_node += " INIT:TRUE";
                    if (!var_type.empty()) ast_node += " TYPE:" + var_type;
                    ast_node += " [3D] " + trf_ast + " }MEID:LID:" + std::to_string(src_line);
                    ast_lines.push_back(pad + ast_node);
                }
                return 0;
            }
            if (left_part.empty() && content.rfind("trf ", 0) == 0) {
                size_t as_pos = content.rfind(" as ");
                if (as_pos != std::string::npos) {
                    std::string expr_body = trim_str(content.substr(4, as_pos - 4));
                    std::string target_var = trim_str(content.substr(as_pos + 4));
                    std::string trf_ast = generate_trf_core_ast(expr_body);
                    if (trf_ast.rfind("ERROR:", 0) == 0) {
                        std::cerr << "编译错误 [行 " << src_line << "]: " << trf_ast << std::endl;
                    } else {
                        uint32_t pid = allocate_address(target_var);
                        char pid_buf[16];
                        snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
                        std::string pid_str(pid_buf);
                        std::string ast_node = "ID:{PID:" + pid_str;
                        ast_node += " {VID:" + target_var + "}";
                        ast_node += " " + std::string(is_const_decl ? "CONST:TRUE" : "CONST:FALSE");
                        if (is_global_decl) ast_node += " GLOBAL:TRUE";
                        if (is_init) ast_node += " INIT:TRUE";
                        if (!var_type.empty()) ast_node += " TYPE:" + var_type;
                        ast_node += " [3D] " + trf_ast + " }MEID:LID:" + std::to_string(src_line);
                        ast_lines.push_back(pad + ast_node);
                    }
                    return 0;
                }
            }

            auto find_top_binary_op = [](const std::string& s, char& out_op) -> size_t {
                int depth = 0;
                bool in_str = false;
                size_t best_pm = std::string::npos;
                size_t best_md = std::string::npos;
                for (size_t k = 0; k < s.size(); ++k) {
                    char c = s[k];
                    if (c == '"') { in_str = !in_str; continue; }
                    if (in_str) continue;
                    if (c == '(') depth++;
                    else if (c == ')') depth--;
                    else if (depth == 0) {
                        if (c == '+' || c == '-') {
                            if (k == 0) continue;
                            char prev = s[k - 1];
                            while (prev == ' ' && k > 1) prev = s[--k - 1];
                            if (prev == '+' || prev == '-' || prev == '*' || prev == '/' ||
                                prev == '(' || prev == ',') continue;
                            best_pm = k;
                        }
                        else if ((c == '*' || c == '/') && k > 0) {
                            if (best_md == std::string::npos) best_md = k;
                        }
                    }
                }
                if (best_pm != std::string::npos) { out_op = s[best_pm]; return best_pm; }
                if (best_md != std::string::npos) { out_op = s[best_md]; return best_md; }
                return std::string::npos;
            };

            auto build_expr_ast = [&](auto&& self, const std::string& raw) -> std::string {
                std::string e = trim_str(raw);
                if (e.empty()) return "NID:0000";
                if (e.size() >= 2 && e.front() == '"' && e.back() == '"') {
                    return "SID:22[" + e.substr(1, e.size() - 2) + "]22";
                }
                char op = 0;
                size_t op_pos = find_top_binary_op(e, op);
                if (op_pos != std::string::npos) {
                    std::string L = self(self, e.substr(0, op_pos));
                    std::string R = self(self, e.substr(op_pos + 1));
                    std::string tag;
                    switch (op) {
                        case '+': tag = "[ADD]"; break;
                        case '-': tag = "[SUB]"; break;
                        case '*': tag = "[MUL]"; break;
                        case '/': tag = "[DIV]"; break;
                    }
                    return L + tag + R;
                }
                {
                    size_t lp = e.find('(');
                    if (lp != std::string::npos) {
                        std::string prefix = trim_str(e.substr(0, lp));
                        size_t dot = prefix.find('.');
                        if (dot != std::string::npos && dot > 0 && dot + 1 < prefix.size()) {
                            std::string obj_name = trim_str(prefix.substr(0, dot));
                            std::string mname    = trim_str(prefix.substr(dot + 1));
                            bool valid = !obj_name.empty() && !mname.empty();
                            for (char c : obj_name) {
                                if (!isalnum((unsigned char)c) && c != '_') { valid = false; break; }
                            }
                            if (valid) {
                                for (char c : mname) {
                                    if (!isalnum((unsigned char)c) && c != '_') { valid = false; break; }
                                }
                            }
                            if (valid) {
                                int d = 1;
                                size_t rp = std::string::npos;
                                for (size_t k = lp + 1; k < e.size(); ++k) {
                                    if (e[k] == '(') d++;
                                    else if (e[k] == ')') { d--; if (d == 0) { rp = k; break; } }
                                }
                                if (rp != std::string::npos) {
                                    std::string args_str = trim_str(e.substr(lp + 1, rp - lp - 1));
                                    std::vector<std::string> arg_list;
                                    if (!args_str.empty()) {
                                        std::string tmp; bool ins = false;
                                        for (char c : args_str) {
                                            if (c == '"') ins = !ins;
                                            if (c == ',' && !ins) {
                                                if (!tmp.empty()) { arg_list.push_back(trim_str(tmp)); tmp.clear(); }
                                            } else tmp += c;
                                        }
                                        if (!tmp.empty()) arg_list.push_back(trim_str(tmp));
                                    }
                                    std::string args_node;
                                    for (size_t k = 0; k < arg_list.size(); ++k) {
                                        if (k > 0) args_node += ",";
                                        args_node += self(self, arg_list[k]);
                                    }
                                    return "FID:METHOD OBJ:VID:" + obj_name
                                        + " NAME:SID:22[" + mname + "]22 ARGS:" + args_node;
                                }
                            }
                        }
                    }
                }
                {
                    size_t lp2 = e.find('(');
                    if (lp2 != std::string::npos) {
                        std::string fname = trim_str(e.substr(0, lp2));
                        bool valid = !fname.empty();
                        for (char c : fname) {
                            if (!isalnum((unsigned char)c) && c != '_') { valid = false; break; }
                        }
                        if (valid && !is_reserved_name(fname)) {
                            int d = 1;
                            size_t rp2 = std::string::npos;
                            for (size_t k = lp2 + 1; k < e.size(); ++k) {
                                if (e[k] == '(') d++;
                                else if (e[k] == ')') { d--; if (d == 0) { rp2 = k; break; } }
                            }
                            if (rp2 != std::string::npos) {
                                std::string args_str = trim_str(e.substr(lp2 + 1, rp2 - lp2 - 1));
                                std::vector<std::string> arg_list;
                                if (!args_str.empty()) {
                                    std::string tmp; bool ins = false; int dep = 0;
                                    for (char c : args_str) {
                                        if (c == '"') ins = !ins;
                                        if (!ins && c == '(') dep++;
                                        else if (!ins && c == ')') dep--;
                                        if (!ins && dep == 0 && c == ',') {
                                           if (!tmp.empty()) { arg_list.push_back(trim_str(tmp)); tmp.clear(); }
                                        } else tmp += c;
                                    }
                                    if (!tmp.empty()) arg_list.push_back(trim_str(tmp));
                                }
                                std::string args_node;
                                for (size_t k = 0; k < arg_list.size(); ++k) {
                                    if (k > 0) args_node += ",";
                                    args_node += self(self, arg_list[k]);
                                }
                                return "FID:CALL NAME:SID:22[" + fname + "]22 ARGS:" + args_node;
                            }
                        }
                    }
                }
                return encode_arg(e);
            };

            size_t first_lp = call_part.find('(');
            bool is_call_like = false;
            if (first_lp != std::string::npos) {
                std::string fname = trim_str(call_part.substr(0, first_lp));
                bool valid = !fname.empty();
                for (char c : fname) {
                    if (!isalnum((unsigned char)c) && c != '_' && c != '.') { valid = false; break; }
                }
                if (valid && !is_reserved_name(fname)) is_call_like = true;
            }

            if (is_call_like) {
                std::string full_expr = build_expr_ast(build_expr_ast, call_part);
                if (!left_part.empty()) {
                    uint32_t pid = allocate_address(left_part);
                    char pid_buf[16];
                    snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
                    std::string pid_str(pid_buf);
                    std::string ast_node = "ID:{PID:" + pid_str;
                    ast_node += " {VID:" + left_part + "}";
                    ast_node += " " + std::string(is_const_decl ? "CONST:TRUE" : "CONST:FALSE");
                    if (is_global_decl) ast_node += " GLOBAL:TRUE";
                    if (is_init) ast_node += " INIT:TRUE";
                    if (!var_type.empty()) ast_node += " TYPE:" + var_type;
                    ast_node += " [3D] " + full_expr + " }MEID:LID:" + std::to_string(src_line);
                    ast_lines.push_back(pad + ast_node);
                } else {
                    ast_lines.push_back(
                        pad + "ID:{" + full_expr + "}MEID:LID:" + std::to_string(src_line));
                }
                return 0;
            }
        }
        // ---------- 普通赋值 ----------
        {
            size_t eq_pos = content.find('=');
            if (eq_pos != std::string::npos) {
                std::string left = trim_str(content.substr(0, eq_pos));
                std::string right = trim_str(content.substr(eq_pos + 1));
                std::string right_node = parse_expression(right);
                uint32_t pid = allocate_address(left);
                char pid_buf[16];
                snprintf(pid_buf, sizeof(pid_buf), "0x%02X", pid);
                std::string pid_str(pid_buf);
                std::string ast_node = "ID:{PID:" + pid_str;
                ast_node += " {VID:" + left + "}";
                ast_node += " " + std::string(is_const_decl ? "CONST:TRUE" : "CONST:FALSE");
                if (is_global_decl) ast_node += " GLOBAL:TRUE";
                if (is_init) ast_node += " INIT:TRUE";
                if (!var_type.empty()) ast_node += " TYPE:" + var_type;
                ast_node += " [3D] " + right_node + " }MEID:LID:" + std::to_string(src_line);
                ast_lines.push_back(pad + ast_node);
            }
        }
        return 0;
    };

    // ============================================================
    // 🚀 主循环：符号 + 缩进双驱动
    // ============================================================
    int lid_counter = 0;
    int prev_indent = 0;

    for (size_t ti = 0; ti < toks.size(); ++ti) {
        auto& t = toks[ti];

        // ① 缩进减少 → 关层（纯缩进输入用；符号式输入缩进恒 0，不触发）
        while (prev_indent > t.indent && !block_stack.empty()) {
            close_block();
            prev_indent = block_stack.empty() ? 0 : block_stack.back().indent;
        }

        // ② 语句识别
        char pending_kind = 0;
        if (!t.stmt.empty()) {
            lid_counter++;
            pending_kind = emit_statement(t.stmt, lid_counter);
        }

        // ③ 符号驱动：\ / $ → 控制语句开层，& → 无条件关层
        if (t.ctrl == '\\' || t.ctrl == '$') {
            if (pending_kind != 0) open_block(pending_kind, t.indent);
        } else if (t.ctrl == '&') {
            close_block();
        }

        // ④ 缩进增加 → 控制语句开层（纯缩进输入用）
        int next_indent = (ti + 1 < toks.size()) ? toks[ti + 1].indent : 0;
        if (pending_kind != 0 && t.ctrl == 0 && next_indent > t.indent) {
            open_block(pending_kind, t.indent);
        }
        prev_indent = t.indent;
    }

    while (!block_stack.empty()) close_block();
    current_ast_level--;
    ast_lines.push_back(std::string(current_ast_level * 4, ' ') + "}");

    return ast_lines;
}