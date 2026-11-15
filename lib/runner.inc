// =================================================================
// runner.inc —— 执行引擎与内存管理
// =================================================================
int execute_xy_code(const std::string& code) {
    if (code.empty()) return -1;

    int v_pid = next_v_pid++;
    size_t estimated_mem = code.size() * 10 + 1024;

    // 🚀 核心修复：补全 DebtTable 和 DebtLevel 参数
    // 由于这里是脚本执行，用完后自己会调用 deallocate，所以放在手动表 + 手动级别即可。
    int debt_id = AMM::getInstance().submit_debt(
        estimated_mem, 
        v_pid, 
        v_pid, 
        DebtTable::MANUAL,         // 🚀 归属手动表
        DebtLevel::MANUAL,         // 🚀 级别为手动
        SIGTERM, 
        DebtLifecycle::INDEFINITE, // 🚀 原参数往后移
        0
    );
    if (debt_id < 0) return -1;

    void* code_mem = AMM::getInstance().commit_memory(debt_id, estimated_mem);
    if (!code_mem) return -1;

    KernelPCB pcb;
    pcb.v_pid = v_pid;
    pcb.name = "xyby_exec";
    pcb.cmd_name = "execute_xy_code";
    pcb.is_running = true;
    pcb.vmem = (VirtualMemoryBlock*)code_mem;
    pcb.pending_kill = false;
    pcb.args = {"exec_code", code};
    process_table[v_pid] = pcb;

    std::map<std::string, std::vector<std::string>> modules;
    std::set<std::string> loading_modules;
    
    // 🚀 安全性增强：用 try-catch 包裹，防止执行脚本发生段错误时导致内存泄漏
    try {
        std::vector<std::string> ast_lines = parse_source_to_ast(code, modules, loading_modules);

        for (const auto& l : ast_lines) {
          // std::cerr << "[AST] " << l << std::endl;
        }

        static Executor executor;
        executor.execute(ast_lines, modules);
    } catch (const std::exception& e) {
        std::cerr << "[Runner] 脚本执行异常: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[Runner] 脚本执行遇到未知异常" << std::endl;
    }

    // 无论成功与否，确保回收内存和进程表
    AMM::getInstance().deallocate(code_mem);
    process_table.erase(v_pid);

    return 0;
}

std::string run_private_script(const std::string& script_content, const std::string& vfs_path, int v_pid) {
    int result = execute_xy_code(script_content);
    if (result == 0) return "";
    std::cout << "[主进程] 脚本执行失败，错误码: " << result << std::endl;
    return "主引擎执行失败，错误码: " + std::to_string(result);
}