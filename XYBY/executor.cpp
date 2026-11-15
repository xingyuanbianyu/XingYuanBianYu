#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <cctype>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <set>
#include <cstdlib>
#include <iomanip>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <optional>

// 🚀 内核调度器
#include "../../Kernel/Scheduler.h"

std::string trim(const std::string& str);
#include "../lib/XYFILE.inc"
#include "config_manager.h"
#include "exec_priority.h"
#include "include/char_id_map.h"

// ==========================================
// 🚀 数据结构定义
// ==========================================
struct FunctionDef {
    std::string name;
    std::vector<std::string> params;
    std::vector<std::string> body;
    std::string inherit_from;
    bool is_async = false;
};

struct ClassDef {
    std::string name;
    std::vector<std::string> parents;
    std::map<std::string, FunctionDef> methods;
};

enum class VarScope { GLOBAL, CLASS, FUNCTION, LOOP, METHOD, MODULE };

struct Variable {
    std::string type;
    std::string value;
    uint32_t address = 0;
    bool is_const = false;
    VarScope scope = VarScope::GLOBAL;
    std::string class_name;
};

struct MemorySpace {
    std::map<std::string, Variable> primary_memory;
    std::vector<std::map<std::string, Variable>> copy_history;
    MemorySpace* global_root = nullptr;

    std::string last_return_value = "";
    bool has_returned = false; // 🚀 修复：防止返回空字符串被忽略
};

struct ReturnSignal { std::string value; };
struct BreakSignal {};
struct ContinueSignal {};

struct LoopFrame {
    enum class Type { FOR, WHILE };
    Type type = Type::FOR;
    std::string var;
    int64_t current = 0;
    int64_t end = 0;
    std::string while_cond;
    size_t body_brace_idx = 0;
    size_t body_start = 0;
    size_t endfor_idx = 0;
    size_t copy_depth = 0;
    int brace_level = 0; // 🚀 修复：记录循环初始层级，解决 break 跳级问题
    int iterations = 0;
    bool is_iter = false;
    std::vector<std::string> iter_values;
    size_t iter_index = 0;
};

// 全局变量声明
extern std::string g_user_cwd;
extern std::string g_script_dir;
std::map<std::string, FunctionDef> m_functions;
std::map<std::string, ClassDef>    m_classes;
std::map<uint32_t, std::map<std::string, Variable>> g_instance_fields;
std::map<std::string, uint32_t> m_variable_address_map;
uint32_t m_next_address = 0x100;

// 🚀【核心修复】把这两个变量提升到全局作用域，解决 main.cpp 链接报错
std::string g_last_xy_return_value = "";
std::string g_await_target_var = "";

#include "global/helpers.inc"
#include "global/memory.inc"

class Executor {
private:
    MemorySpace main_memory;
    std::map<std::string, MemorySpace> module_memory;
    std::map<std::string, std::vector<std::string>>* modules_ast = nullptr;
    std::map<std::string, Variable*> global_variable_index;

    std::map<std::string, ConfigType> m_module_types;
    ConfigManager m_config;
    ExecPriorityResolver m_resolver;

    std::set<std::string> m_executing_modules;
    std::vector<LoopFrame> m_loop_stack;
    std::string m_last_print_output = "";

    std::vector<std::string> g_class_stack;
    std::vector<int>         g_class_brace_stack;
    int g_func_depth = 0;

    static constexpr int MAX_LOOP_ITERATIONS = 100000;

    // 🚀 修复作用域链条：局部 -> 主内存 -> 全局索引
    std::string resolve_deep(const std::string& var_name, MemorySpace& mem) {
        std::set<std::string> visited;
        std::string current = var_name;
        int max_depth = 100;

        while (max_depth-- > 0) {
            if (visited.count(current)) throw std::runtime_error("[ERROR] 变量循环引用: " + current);
            visited.insert(current);

            Variable* var = nullptr;

            // 1. 局部作用域栈 (倒序查找)
            for (auto it = mem.copy_history.rbegin(); it != mem.copy_history.rend(); ++it) {
                auto fit = it->find(current);
                if (fit != it->end()) { var = &fit->second; break; }
            }
            // 2. 主内存
            if (!var) {
                auto it = mem.primary_memory.find(current);
                if (it != mem.primary_memory.end()) var = &it->second;
            }
            // 3. 全局索引
            if (!var) {
                auto git = global_variable_index.find(current);
                if (git != global_variable_index.end()) var = git->second;
            }

            if (!var) throw std::runtime_error("致命错误: 变量未定义 -> " + current);

            if (var->type == "STR" || var->type == "STRING") {
                std::string next_name = var->value;
                bool next_exists = false;
                for (auto it = mem.copy_history.rbegin(); it != mem.copy_history.rend(); ++it)
                    if (it->find(next_name) != it->end()) { next_exists = true; break; }
                if (!next_exists && mem.primary_memory.find(next_name) != mem.primary_memory.end()) next_exists = true;
                else if (!next_exists && global_variable_index.find(next_name) != global_variable_index.end()) next_exists = true;

                if (next_exists) { current = next_name; continue; }
            }
            return var->value;
        }
        throw std::runtime_error("[ERROR] 变量解析深度超限");
    }

    // 引入所有子模块
    #include "lib/AS.inc"
    #include "lib/INPUT.inc"
    #include "lib/MODULE.inc"
    #include "expr/eval_helper.inc"
    #include "expr/eval_template.inc"
    #include "expr/eval_native_io.inc"
    #include "expr/eval_builtin_num.inc"
    #include "expr/eval_builtin_str.inc"
    #include "expr/eval_builtin_misc.inc"
    #include "expr/eval_builtin.inc"
    #include "expr/eval_user_func.inc" // 🚀 实现就包含在这里面！
    #include "expr/eval_math.inc"
    #include "expr/eval_cmp.inc"
    #include "expr/eval_arith.inc"
    #include "expr/eval_literal.inc"
    #include "expr/eval_expr.inc"
    #include "assign/handle_assign.inc"
    #include "CFG/compare_values.inc"
    #include "CFG/extract_cmp_op.inc"
    #include "CFG/extract_condition.inc"
    #include "CFG/has_sibling_branch.inc"
    #include "CFG/eval_bool_expr.inc"
    #include "CFG/evaluate_condition.inc"
    #include "CFG/evaluate_while_condition.inc"

    #include "lib/OR.inc"
    #include "lib/AND.inc"
    #include "lib/NOT.inc"
    #include "lib/IF.inc"
    #include "lib/ELIF.inc"
    #include "lib/ELSE.inc"
    #include "lib/IN.inc"
    #include "lib/RANGE.inc"
    #include "lib/FOR.inc"
    #include "lib/WHILE.inc"
    #include "lib/BREAK.inc"
    #include "lib/CONTINUE.inc"
    #include "lib/IMPORT.inc"
    #include "lib/FROM.inc"
    #include "lib/CLASS.inc"
    #include "lib/GLOBAL.inc"
    #include "lib/PASS.inc"
    #include "lib/AWAIT.inc"
    #include "lib/OPEN.inc"
    #include "lib/PRINT.inc"
    #include "lib/DEF.inc"
    #include "lib/RETURN.inc"
    #include "lib/BUILD.inc"
    #include "lib/DECOMPILE.inc"
    #include "lib/DEL.inc"
    #include "lib/WITH.inc"
    #include "lib/BIN7.inc"
    #include "lib/BIN7_DECODE.inc"
    #include "lib/XYCREATE.inc"

    void executeBlock(const std::vector<std::string>& lines, MemorySpace& mem) {
        if (lines.empty()) return;

        int brace_level = 0;
        int skip_level = -1;
        std::map<int, bool> if_passed;

        if (mem.copy_history.empty()) mem.copy_history.push_back({});

        for (size_t i = 0; i < lines.size(); ++i) {
            const std::string& line = lines[i];
            if (line.empty()) continue;
            std::string trimLine = trim(line);

            if (trimLine == "{") {
                brace_level++;
                if (skip_level == -1) mem.copy_history.push_back({});
                continue;
            }
            
            if (trimLine == "}") {
                brace_level--;
                if (skip_level != -1 && brace_level < skip_level) skip_level = -1;

                while (!g_class_brace_stack.empty() && brace_level == g_class_brace_stack.back()) {
                    g_class_stack.pop_back();
                    g_class_brace_stack.pop_back();
                }

                // 如果处于跳过状态，不弹栈（因为入栈时没压入）
                if (skip_level == -1 && mem.copy_history.size() > 1) {
                    mem.copy_history.pop_back();
                }
                on_brace_close(brace_level);
                continue;
            }
            
            if (skip_level != -1) continue;

            // 执行具体语句，传入 brace_level 和 skip_level 供控制流使用
            if (trimLine.find("FID:writefile") != std::string::npos || trimLine.find("FID:readfile") != std::string::npos) {
                eval_expr(trimLine, mem); continue;
            }

            if (handlePrint(line, mem)) continue;
            if (handleClass(line, mem, brace_level)) continue;
            
            // 🚀 传入 brace_level 和 skip_level
            if (handleWhile(line, lines, i, brace_level, mem, skip_level)) continue;
            if (handleEndWhile(line, mem, i)) continue;
            if (handleFor(line, lines, i, brace_level, mem, skip_level)) continue;
            if (handleEndFor(line, mem, i)) continue;
            
            // 🚀 BREAK/CONTINUE 使用 skip_level 机制
            if (handleBreak(line, mem, i, brace_level, skip_level)) continue;
            if (handleContinue(line, mem, i, brace_level, skip_level)) continue;
            
            if (handlePass(line, mem)) continue;

            if (handleIf(line, mem, if_passed, brace_level, skip_level, lines, i)) continue;
            if (handleElif(line, mem, if_passed, brace_level, skip_level)) continue;
            if (handleElse(line, mem, if_passed, brace_level, skip_level)) continue;

            {
                size_t skip_to = i;
                if (handleDef(line, lines, i, brace_level, skip_to)) { i = skip_to; continue; }
            }

            if (handleGlobal(line, mem)) continue;
            if (handleReturn(line, mem)) continue;
            if (handleBuild(line, mem, if_passed, brace_level, skip_level, lines, i)) continue;
            if (handleDecompile(line, mem)) continue;
            if (handleDel(line, mem)) continue;
            if (handleAwait(line, mem)) continue;
            if (handleCall(line, mem)) continue;
            if (handleImport(line, mem)) continue;
            if (handleFrom(line, mem)) continue;

            // 配置处理略过 (保持原样)
            if (line.find("FID:SET_CONFIG") != std::string::npos) { continue; }
            if (line.find("FID:GET_CONFIG") != std::string::npos) { continue; }

            if (handleWithOpen(line, mem)) continue;
            if (handleOpen(line, mem)) continue;
            if (handleBin7Decode(line, mem)) continue;
            if (handleBin7(line, mem)) continue;
            if (handleXYCreate(line, mem)) continue;

            if (handleAssign(line, mem)) continue;
        }
    }

public:
    std::string execute_and_get_return(const std::vector<std::string>& lines) {
        main_memory.last_return_value = "";
        main_memory.has_returned = false;
        try {
            executeBlock(lines, main_memory);
        } catch (const ReturnSignal&) {
        } catch (const BreakSignal&) {
        } catch (const ContinueSignal&) {
        } catch (const std::exception& e) {
            std::cerr << "运行时异常: " << e.what() << std::endl;
        }
        return main_memory.has_returned ? main_memory.last_return_value : "";
    }

    void execute(const std::vector<std::string>& lines, const std::map<std::string, std::vector<std::string>>& modules) {
        modules_ast = const_cast<std::map<std::string, std::vector<std::string>>*>(&modules);
        main_memory.copy_history.clear();
        main_memory.copy_history.push_back({});

        try {
            executeBlock(lines, main_memory);
        } catch (const ReturnSignal&) {
        } catch (const BreakSignal&) {
        } catch (const ContinueSignal&) {
        } catch (const std::exception& e) {
            std::cerr << "执行崩溃: " << e.what() << std::endl;
        }

        main_memory.copy_history.clear();
        if (main_memory.has_returned) {
            g_last_xy_return_value = main_memory.last_return_value; // 🚀 给全局变量赋值
        }
    }
};

static Executor g_executor;