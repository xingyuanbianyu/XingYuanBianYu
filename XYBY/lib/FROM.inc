// ============================================================
// FROM.inc —— 精准导入指定函数 (FID:FROM)
// 格式: FID:FROM>MOD:SID:22[模块路径]22>FUNCS:SID:22[函数1]22,SID:22[函数2]22
// ============================================================
bool handleFrom(const std::string& line, MemorySpace& mem) {
    if (line.find("KID:FROM") == std::string::npos) return false;

    // 1. 解析模块路径
    size_t mod_pos = line.find("MOD:SID:22[");
    if (mod_pos == std::string::npos) return false;
    size_t mod_end = line.find("]22", mod_pos + 11);
    if (mod_end == std::string::npos) return false;
    std::string module_name = trim(line.substr(mod_pos + 11, mod_end - mod_pos - 11));
    
    // 清除可能存在的双引号，如 "add.xyd" -> add.xyd
    if (module_name.size() >= 2 && module_name.front() == '"' && module_name.back() == '"') {
        module_name = module_name.substr(1, module_name.size() - 2);
    }

    // 2. 解析函数列表（支持逗号分隔）
    size_t funcs_pos = line.find("FUNCS:SID:22[");
    if (funcs_pos == std::string::npos) return false;
    size_t funcs_end = line.find("]22", funcs_pos + 13);
    if (funcs_end == std::string::npos) return false;
    std::string funcs_str = line.substr(funcs_pos + 13, funcs_end - funcs_pos - 13);

    std::vector<std::string> target_funcs;
    std::stringstream ss(funcs_str);
    std::string fname;
    while (std::getline(ss, fname, ',')) {
        target_funcs.push_back(trim(fname));
    }

    // 3. 【核心】自动加载模块，如果模块不在 AST 里，直接触发加载！
    if (!modules_ast || modules_ast->find(module_name) == modules_ast->end()) {
        // 伪装成 import 指令，让 IMPORT.inc 去完成文件读取和 AST 解析
        std::string fake_import = "ID:{IMP:" + module_name + "}";
        
        // 这里调用你的 IMPORT.inc 里的加载函数。
        // 建议把 IMPORT.inc 里的加载逻辑抽出一个独立函数 `bool load_module_to_ast(const std::string&)`
        // 然后再调用，避免直接调用 handleImport 带来的重复执行逻辑。
        // 这里我们先假设可以直接调用 handleImport 来把 AST 拉进来。
        handleImport(fake_import, mem); 

        // 如果拉取后还是找不到，说明文件真不存在
        if (!modules_ast || modules_ast->find(module_name) == modules_ast->end()) {
            std::cout << "错误: 无法加载模块 '" << module_name << "'，请检查路径" << std::endl;
            return true;
        }
    }

    const auto& mod_lines = modules_ast->at(module_name);

    // 4. 在模块 AST 中寻找目标函数并注册
    for (const auto& func_name : target_funcs) {
        bool found = false;
        for (size_t i = 0; i < mod_lines.size(); ++i) {
            std::string tl = trim(mod_lines[i]);
            
            // 【严格遵照规范】关键字用 KID，函数名在 NAME:SID:22[...]22 中
            std::string search_str = "NAME:SID:22[" + func_name + "]22";
            
            if (tl.find("KID:DEF") != std::string::npos && tl.find(search_str) != std::string::npos) {
                found = true;
                size_t skip_to = i;
                
                // 精准调用 handleDef，把函数直接注册到当前的 `mem` 空间
                handleDef(mod_lines[i], mod_lines, i, 0, skip_to);
                break;
            }
        }
        if (!found) {
            std::cout << "警告: 模块 '" << module_name << "' 中找不到函数 '" << func_name << "'" << std::endl;
        }
    }

    return true;
}