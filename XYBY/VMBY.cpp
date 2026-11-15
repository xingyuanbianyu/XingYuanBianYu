// ============================================================
// VMBY.cpp —— 公共库 + 调度层
// ============================================================
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <fstream>
#include <sstream>
#include <map>
#include <set>
#include "../lib/VM.inc"

// ============================================================
// 公共全局状态
// ============================================================
static std::map<std::string, size_t> g_func_table;
static std::vector<std::pair<std::string, size_t>> g_call_sites;
static std::set<std::string> g_class_set;
static std::map<char, std::string> g_var_class;
static std::string g_current_class = "";
static std::string g_current_parent = "";

static std::string trim(const std::string& s) {
size_t start = s.find_first_not_of(" \t\r\n");
if (start == std::string::npos) return "";
size_t end = s.find_last_not_of(" \t\r\n");
return s.substr(start, end - start + 1);
}

// ============================================================
// 栈式辅助函数
// ============================================================
static size_t find_last_bin_op(const std::string& expr, std::string& out_tag, OpCode& out_code);
static void emitOperand(const std::string& part, std::vector<uint8_t>& bytes, size_t& inst_count);

static void emitUnaryNegate(const std::string& inner, std::vector<uint8_t>& bytes, size_t& inst_count) {
bytes.push_back(static_cast<uint8_t>(OP_PUSH));
bytes.push_back(0);
inst_count++;
emitOperand(inner, bytes, inst_count);
bytes.push_back(static_cast<uint8_t>(OP_SUB));
inst_count++;
}

static void emitOperand(const std::string& part, std::vector<uint8_t>& bytes, size_t& inst_count) {
// 🚀 {G ...} 分组剥离
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
emitOperand(inner, bytes, inst_count);
return;
}
}
}

// ============================================================
// 🚀 【新增拦截】内置输入指令（必须在所有匹配之前拦截）
// ============================================================
if (part.find("FID:INPUT") != std::string::npos) {
size_t prompt_pos = part.find("SID:22[");
if (prompt_pos != std::string::npos) {
size_t start = prompt_pos + 7;
size_t end = part.find("]22", start);
if (end != std::string::npos) {
std::string prompt = part.substr(start, end - start);
bytes.push_back(static_cast<uint8_t>(OP_PUSH_S));
uint16_t len = (uint16_t)prompt.size();
bytes.push_back((uint8_t)(len & 0xFF));
bytes.push_back((uint8_t)((len >> 8) & 0xFF));
for (char c : prompt) bytes.push_back((uint8_t)c);
inst_count++;
bytes.push_back(static_cast<uint8_t>(OP_PRINT_NO_NL)); 
inst_count++;
}
}
bytes.push_back(static_cast<uint8_t>(OP_INPUT));
inst_count++;
return;
}
// ============================================================

// ---------- 方法调用 ----------
if (part.find("FID:METHOD") != std::string::npos) {
size_t obj_pos = part.find("OBJ:VID:");
if (obj_pos == std::string::npos) {
bytes.push_back(static_cast<uint8_t>(OP_PUSH));
bytes.push_back(0);
inst_count++;
return;
}
char obj_char = part[obj_pos + 8];
size_t name_start = part.find("NAME:SID:22[");
if (name_start == std::string::npos) {
bytes.push_back(static_cast<uint8_t>(OP_PUSH));
bytes.push_back(0);
inst_count++;
return;
}
name_start += 12;
size_t name_end = part.find("]22", name_start);
if (name_end == std::string::npos) name_end = part.size();
std::string meth_name = part.substr(name_start, name_end - name_start);
auto it = g_var_class.find(obj_char);
if (it == g_var_class.end()) {
std::cerr << "[编译错误] 变量 " << obj_char << " 不是实例\n";
bytes.push_back(static_cast<uint8_t>(OP_PUSH));
bytes.push_back(0);
inst_count++;
return;
}
std::string full_name = it->second + "." + meth_name;
bytes.push_back(static_cast<uint8_t>(OP_LOAD));
bytes.push_back(static_cast<uint8_t>(obj_char));
inst_count++;
size_t args_start = part.find("ARGS:");
if (args_start != std::string::npos) {
args_start += 5;
std::string args = part.substr(args_start);
while (!args.empty() && args.back() == ' ') args.pop_back();
std::vector<std::string> arg_list;
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
for (const auto& a : arg_list) emitOperand(a, bytes, inst_count);
}
bytes.push_back(static_cast<uint8_t>(OP_CALL));
size_t patch_pos = bytes.size();
bytes.push_back(0x00);
inst_count++;
g_call_sites.push_back({full_name, patch_pos});
return;
}

// ---------- 普通函数调用 ----------
if (part.find("FID:CALL") != std::string::npos) {
size_t name_start = part.find("NAME:SID:22[");
if (name_start == std::string::npos) {
bytes.push_back(static_cast<uint8_t>(OP_PUSH));
bytes.push_back(0);
inst_count++;
return;
}
name_start += 12;
size_t name_end = part.find("]22", name_start);
if (name_end == std::string::npos) name_end = part.size();
std::string func_name = part.substr(name_start, name_end - name_start);
size_t args_start = part.find("ARGS:");
if (args_start != std::string::npos) {
args_start += 5;
std::string args = part.substr(args_start);
while (!args.empty() && args.back() == ' ') args.pop_back();
std::vector<std::string> arg_list;
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
for (const auto& a : arg_list) emitOperand(a, bytes, inst_count);
}
bytes.push_back(static_cast<uint8_t>(OP_CALL));
size_t patch_pos = bytes.size();
bytes.push_back(0x00);
inst_count++;
g_call_sites.push_back({func_name, patch_pos});
return;
}

// ============================================================
// 🚀 二元运算：在方法调用和函数调用之后处理
// ============================================================
{
std::string s = trim(part);
std::string op_tag;
OpCode op_code = OP_NOP;
size_t op_pos = find_last_bin_op(s, op_tag, op_code);
if (op_pos != std::string::npos) {
std::string left  = s.substr(0, op_pos);
std::string right = s.substr(op_pos + op_tag.length());
emitOperand(left,  bytes, inst_count);
emitOperand(right, bytes, inst_count);
bytes.push_back(static_cast<uint8_t>(op_code));
inst_count++;
return;
}
}

// ---------- 一元负号 ----------
{
size_t sub_pos = part.find("[SUB]");
if (sub_pos != std::string::npos) {
std::string before = trim(part.substr(0, sub_pos));
if (before.empty()) {
std::string inner = trim(part.substr(sub_pos + 5));
if (!inner.empty()) {
emitUnaryNegate(inner, bytes, inst_count);
return;
}
}
}
}

// ---------- 变量 ----------
size_t vid_pos = part.find("VID:");
if (vid_pos != std::string::npos) {
size_t start = vid_pos + 4;
size_t end = start;
while (end < part.size() &&
(isalnum((unsigned char)part[end]) || part[end] == '_')) end++;
if (end > start) {
bytes.push_back(static_cast<uint8_t>(OP_LOAD));
bytes.push_back(static_cast<uint8_t>(part[start]));
inst_count++;
return;
}
}

// ---------- 浮点 ----------
size_t flt_pos = part.find("FLT:");
if (flt_pos != std::string::npos) {
size_t start = flt_pos + 4;
size_t end = start;
while (end < part.size() &&
(isdigit((unsigned char)part[end]) || part[end] == '.' ||
part[end] == '-' || part[end] == '+' ||
part[end] == 'e' || part[end] == 'E')) end++;
if (end > start) {
try {
std::string numstr = part.substr(start, end - start);
double d = std::stod(numstr);
int prec = 0;
size_t dot = numstr.find('.');
if (dot != std::string::npos) {
for (size_t k = dot + 1; k < numstr.size(); ++k) {
if (isdigit((unsigned char)numstr[k])) prec++;
else break;
}
}
bytes.push_back(static_cast<uint8_t>(OP_PUSH_F));
uint64_t bits = 0;
std::memcpy(&bits, &d, 8);
for (int k = 0; k < 8; ++k)
bytes.push_back((uint8_t)((bits >> (8 * k)) & 0xFF));
bytes.push_back((uint8_t)(prec & 0xFF));
inst_count++;
return;
} catch (...) {}
}
}

// ---------- 字符串 ----------
size_t sid_pos = part.find("SID:22[");
if (sid_pos != std::string::npos) {
size_t start = sid_pos + 7;
size_t end = part.find("]22", start);
if (end == std::string::npos) end = part.size();
std::string content = part.substr(start, end - start);
if (content.size() <= 65535) {
bytes.push_back(static_cast<uint8_t>(OP_PUSH_S));
uint16_t len = (uint16_t)content.size();
bytes.push_back((uint8_t)(len & 0xFF));
bytes.push_back((uint8_t)((len >> 8) & 0xFF));
for (char c : content) bytes.push_back((uint8_t)c);
inst_count++;
}
return;
}

// ---------- 整数 ----------
size_t nid_pos = part.find("NID:");
if (nid_pos != std::string::npos) {
size_t start = nid_pos + 4;
size_t end = start;
if (start < part.size() && part[start] == '-') start++;
while (end < part.size() && isdigit((unsigned char)part[end])) end++;
if (end > start) {
int val = std::stoi(part.substr(nid_pos + 4, end - (nid_pos + 4)));
bytes.push_back(static_cast<uint8_t>(OP_PUSH));
bytes.push_back(static_cast<uint8_t>(val & 0xFF));
inst_count++;
return;
}
}

// ---------- 兜底 ----------
bytes.push_back(static_cast<uint8_t>(OP_PUSH));
bytes.push_back(0);
inst_count++;
}

// ============================================================
// 运算符表
// ============================================================
struct OpInfo { const char* tag; OpCode code; };
static const OpInfo BIN_OPS[] = {
{"[ADD]", OP_ADD}, {"[SUB]", OP_SUB}, {"[MUL]", OP_MUL},
{"[DIV]", OP_DIV}, {"[IDIV]", OP_IDIV}, {"[POW]", OP_POW},
};

// ============================================================
// 🚀 find_last_bin_op
// ============================================================
static size_t find_last_bin_op(const std::string& expr, std::string& out_tag, OpCode& out_code) {
size_t best_pos  = std::string::npos;
int    best_prio = 999;
int    depth = 0;

for (size_t i = 0; i < expr.size(); ++i) {
if (expr[i] == '(' || expr[i] == '{') depth++;
else if (expr[i] == ')' || expr[i] == '}') depth--;
else if (depth == 0) {
for (const auto& op : BIN_OPS) {
size_t tag_len = std::strlen(op.tag);
if (i + tag_len <= expr.size() && expr.compare(i, tag_len, op.tag) == 0) {
std::string before = trim(expr.substr(0, i));
if (op.tag[1]=='S' && op.tag[2]=='U' && op.tag[3]=='B' && before.empty()) {
i += tag_len - 1;
break;
}
int prio = 2;
if      (op.tag[1]=='A') prio = 1;
else if (op.tag[1]=='S') prio = 1;
else if (op.tag[1]=='M') prio = 2;
else if (op.tag[1]=='D') prio = 2;
else if (op.tag[1]=='I') prio = 2;
else if (op.tag[1]=='P') prio = 3;

bool take = (prio < best_prio) ||
(prio == best_prio &&
(best_pos == std::string::npos || i > best_pos));
if (take) {
best_prio = prio;
best_pos  = i;
out_tag   = op.tag;
out_code  = op.code;
}
i += tag_len - 1;
break;
}
}
}
}
return best_pos;
}

// ---------- 赋值 ----------
static bool compileAssign(const std::string& line, std::vector<uint8_t>& bytes, size_t& inst_count) {
size_t vid_pos = line.find("{VID:");
if (vid_pos == std::string::npos) return false;
size_t vid_end = line.find("}", vid_pos);
if (vid_end == std::string::npos) return false;
std::string var_name = line.substr(vid_pos + 5, vid_end - vid_pos - 5);
if (var_name.empty()) return false;
char var_char = var_name[0];

size_t threeD_pos = line.find("[3D]");
if (threeD_pos == std::string::npos) return false;
size_t expr_start = threeD_pos + 4;
size_t expr_end = line.find("}MEID", expr_start);
if (expr_end == std::string::npos) expr_end = line.size();
std::string expr = line.substr(expr_start, expr_end - expr_start);

bool is_init = (line.find("INIT:TRUE") != std::string::npos);
std::string var_type = "";
if (line.find("TYPE:INT") != std::string::npos) var_type = "INT";
else if (line.find("TYPE:STR") != std::string::npos) var_type = "STR";

if (is_init && !var_type.empty()) {
if (var_type == "INT") {
if (expr.find("SID:") != std::string::npos) {
std::cerr << "[编译错误] 强类型拦截：int 变量 " << var_name << " 不能赋值为字符串！\n";
return false;
}
} else if (var_type == "STR") {
if (expr.find("SID:") == std::string::npos && expr.find("NID:") != std::string::npos) {
std::cerr << "[编译错误] 强类型拦截：str 变量 " << var_name << " 不能直接赋值为数字！(请加引号)\n";
return false;
}
}
}

if (expr.find("FID:CALL") != std::string::npos) {
size_t ns = expr.find("NAME:SID:22[");
if (ns != std::string::npos) {
ns += 12;
size_t ne = expr.find("]22", ns);
if (ne != std::string::npos) {
std::string call_name = expr.substr(ns, ne - ns);
if (g_class_set.count(call_name)) {
g_var_class[var_char] = call_name;
bytes.push_back(static_cast<uint8_t>(OP_PUSH));
bytes.push_back(0);
inst_count++;
bytes.push_back(static_cast<uint8_t>(OP_STORE));
bytes.push_back(static_cast<uint8_t>(var_char));
inst_count++;
return true;
}
}
}
}

if (expr.find("FID:CALL") != std::string::npos ||
expr.find("FID:METHOD") != std::string::npos ||
expr.find("FID:INPUT") != std::string::npos) {
emitOperand(expr, bytes, inst_count);
} else {
std::string op_tag;
OpCode op_code = OP_NOP;
size_t op_pos = find_last_bin_op(expr, op_tag, op_code);
if (op_pos != std::string::npos) {
std::string left_part  = expr.substr(0, op_pos);
std::string right_part = expr.substr(op_pos + op_tag.length());
emitOperand(left_part,  bytes, inst_count);
emitOperand(right_part, bytes, inst_count);
bytes.push_back(static_cast<uint8_t>(op_code));
inst_count++;
} else {
emitOperand(expr, bytes, inst_count);
}
}

bytes.push_back(static_cast<uint8_t>(OP_STORE));
bytes.push_back(static_cast<uint8_t>(var_char));
inst_count++;
return true;
}

// ---------- 打印 ----------
static bool compilePrint(const std::string& line, std::vector<uint8_t>& bytes, size_t& inst_count) {
if (line.find("FID:PRINT") == std::string::npos) return false;
size_t start = line.find("FID:PRINT") + 9;
while (start < line.size() && line[start] == ' ') start++;
size_t end = line.find("}}MEID");
if (end == std::string::npos) end = line.find("}MEID");
if (end == std::string::npos) return false;
std::string args = line.substr(start, end - start);

if (args.find("FID:CALL") != std::string::npos ||
args.find("FID:METHOD") != std::string::npos ||
args.find("FID:INPUT") != std::string::npos) {
emitOperand(args, bytes, inst_count);
bytes.push_back(static_cast<uint8_t>(OP_PRINT));
inst_count++;
return true;
}

size_t or_pos  = args.rfind("[OR]");
size_t and_pos = args.rfind("[AND]");

if (or_pos != std::string::npos) {
std::string left  = args.substr(0, or_pos);
std::string right = args.substr(or_pos + 4);
emitOperand(left,  bytes, inst_count);
emitOperand(right, bytes, inst_count);
bytes.push_back(static_cast<uint8_t>(OP_OR));
inst_count++;
bytes.push_back(static_cast<uint8_t>(OP_PRINT));
inst_count++;
return true;
}
if (and_pos != std::string::npos) {
std::string left  = args.substr(0, and_pos);
std::string right = args.substr(and_pos + 5);
emitOperand(left,  bytes, inst_count);
emitOperand(right, bytes, inst_count);
bytes.push_back(static_cast<uint8_t>(OP_AND));
inst_count++;
bytes.push_back(static_cast<uint8_t>(OP_PRINT));
inst_count++;
return true;
}
if (args.rfind("[NOT]", 0) == 0) {
std::string operand = args.substr(5);
emitOperand(operand, bytes, inst_count);
bytes.push_back(static_cast<uint8_t>(OP_NOT));
inst_count++;
bytes.push_back(static_cast<uint8_t>(OP_PRINT));
inst_count++;
return true;
}

std::string op_tag;
OpCode op_code = OP_NOP;
size_t op_pos = find_last_bin_op(args, op_tag, op_code);
if (op_pos != std::string::npos) {
std::string left_part  = args.substr(0, op_pos);
std::string right_part = args.substr(op_pos + op_tag.length());
emitOperand(left_part,  bytes, inst_count);
emitOperand(right_part, bytes, inst_count);
bytes.push_back(static_cast<uint8_t>(op_code));
inst_count++;
} else {
emitOperand(args, bytes, inst_count);
}
bytes.push_back(static_cast<uint8_t>(OP_PRINT));
inst_count++;
return true;
}

// ============================================================
// 条件求值器
// ============================================================
static bool emitCondition(const std::string& raw_cond, std::vector<uint8_t>& bytes, size_t& inst_count) {
std::string cond = trim(raw_cond);
if (cond.empty()) return false;

if (cond.find("BOOL:TRUE") != std::string::npos ||
cond == "True" || cond == "true") {
bytes.push_back(static_cast<uint8_t>(OP_TRUE));
inst_count++;
return true;
}
if (cond.find("BOOL:FALSE") != std::string::npos ||
cond == "False" || cond == "false") {
bytes.push_back(static_cast<uint8_t>(OP_FALSE));
inst_count++;
return true;
}

size_t or_pos = cond.rfind("[OR]");
if (or_pos != std::string::npos) {
std::string left  = trim(cond.substr(0, or_pos));
std::string right = trim(cond.substr(or_pos + 4));
if (!emitCondition(left,  bytes, inst_count)) return false;
if (!emitCondition(right, bytes, inst_count)) return false;
bytes.push_back(static_cast<uint8_t>(OP_OR));
inst_count++;
return true;
}
size_t and_pos = cond.rfind("[AND]");
if (and_pos != std::string::npos) {
std::string left  = trim(cond.substr(0, and_pos));
std::string right = trim(cond.substr(and_pos + 5));
if (!emitCondition(left,  bytes, inst_count)) return false;
if (!emitCondition(right, bytes, inst_count)) return false;
bytes.push_back(static_cast<uint8_t>(OP_AND));
inst_count++;
return true;
}
if (cond.rfind("[NOT]", 0) == 0) {
std::string operand = trim(cond.substr(5));
if (!emitCondition(operand, bytes, inst_count)) return false;
bytes.push_back(static_cast<uint8_t>(OP_NOT));
inst_count++;
return true;
}

std::string op_symbol;
size_t op_pos = 0;
if      ((op_pos = cond.find("[EQ]")) != std::string::npos) op_symbol = "EQ";
else if ((op_pos = cond.find("[LT]")) != std::string::npos) op_symbol = "LT";
else if ((op_pos = cond.find("[GT]")) != std::string::npos) op_symbol = "GT";
else if ((op_pos = cond.find("[NE]")) != std::string::npos) op_symbol = "NE";
else if ((op_pos = cond.find("[GE]")) != std::string::npos) op_symbol = "GE";
else if ((op_pos = cond.find("[LE]")) != std::string::npos) op_symbol = "LE";

if (!op_symbol.empty()) {
std::string left_part  = cond.substr(0, op_pos);
std::string right_part = cond.substr(op_pos + op_symbol.length() + 2);
emitOperand(left_part,  bytes, inst_count);
emitOperand(right_part, bytes, inst_count);
if      (op_symbol == "EQ") bytes.push_back(static_cast<uint8_t>(OP_EQUAL));
else if (op_symbol == "LT") bytes.push_back(static_cast<uint8_t>(OP_LESS));
else if (op_symbol == "GT") bytes.push_back(static_cast<uint8_t>(OP_GREATER));
else if (op_symbol == "GE") bytes.push_back(static_cast<uint8_t>(OP_GREATER));
else if (op_symbol == "LE") bytes.push_back(static_cast<uint8_t>(OP_LESS));
else if (op_symbol == "NE") bytes.push_back(static_cast<uint8_t>(OP_EQUAL));
inst_count++;
return true;
}
emitOperand(cond, bytes, inst_count);
return true;
}

static bool parseCondition(const std::string& line, std::vector<uint8_t>& bytes, size_t& inst_count) {
size_t cond_pos = line.find("COND:");
if (cond_pos == std::string::npos) return false;
cond_pos += 5;
size_t cond_end = line.find("}", cond_pos);
if (cond_end == std::string::npos) cond_end = line.size();
std::string cond_str = line.substr(cond_pos, cond_end - cond_pos);
return emitCondition(cond_str, bytes, inst_count);
}

// ============================================================
// 🚀 三个编译器后端
// ============================================================
#include "lib/lib0.inc"
#include "lib/lib1.inc"
#include "lib/lib2.inc"