import os

def escape_cpp(s):
    # 转义 C++ 字符串中的特殊字符
    return s.replace('\\', '\\\\').replace('"', '\\"')

def generate_map_from_file(input_file, output_file, map_name):
    if not os.path.exists(input_file):
        print(f"跳过: 找不到 {input_file}")
        return

    with open(input_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    cpp = [
        '#pragma once\n',
        '#include <string>\n',
        '#include <unordered_map>\n\n',
        f'inline const std::unordered_map<std::string, std::string> {map_name} = {{\n'
    ]
    
    count = 0
    for line in lines:
        line = line.strip()
        if not line or line.startswith('#') or line.startswith('//'): continue
        
        key = ""
        val = ""
        
        # 【核心适配逻辑】根据不同的分隔符解析
        if '==' in line:
            # 适配 英文操作码.xy, 拼音.xy, 拼音操作码.xy
            parts = line.split('==')
            if len(parts) == 2:
                key = parts[0].strip()
                val = parts[1].strip()
        elif ':' in line:
            # 适配 zh.xy（带引号的中文映射）
            parts = line.split(':', 1)
            if len(parts) == 2:
                key = parts[0].strip()
                val = parts[1].strip()
                # 【核心】去掉两侧可能存在的双引号
                key = key.strip('"')
                val = val.strip('"')
        
        if key and val:
            key = escape_cpp(key)
            val = escape_cpp(val)
            cpp.append(f'    {{"{key}", "{val}"}},\n')
            count += 1
            
    cpp.append('};\n')

    with open(output_file, 'w', encoding='utf-8') as f:
        f.writelines(cpp)
    print(f"✅ 生成 {output_file} (共 {count} 条)")

# 执行生成（文件名和映射名一一对应）
generate_map_from_file('en.xy', 'lang_en_map.h', 'LANG_EN_MAP')
generate_map_from_file('zh.xy', 'lang_zh_map.h', 'LANG_ZH_MAP')
generate_map_from_file('拼音.xy', 'pinyin_map.h', 'PINYIN_MAP')
generate_map_from_file('拼音操作码.xy', 'pinyin_opcode_map.h', 'PINYIN_OPCODE_MAP')