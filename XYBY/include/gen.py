# gen.py
from pypinyin import pinyin, Style

def generate_full_map():
    print("正在自动提取所有中文汉字，请稍候...")
    
    count = 0
    with open("zh_pinyin_map.h", "w", encoding="utf-8") as f:
        f.write("// ==========================================\n")
        f.write("// 自动生成的终极中文-拼音映射表 (编码表)\n")
        f.write("// 包含：Unicode 标准 CJK 统一表意文字\n")
        f.write("// ==========================================\n")
        f.write("#pragma once\n#include <string>\n#include <unordered_map>\n\n")
        f.write("inline const std::unordered_map<std::string, std::string> ZH_PINYIN_MAP = {\n")

        # 遍历 Unicode 中的中文字符范围 0x4E00 到 0x9FA5
        for code in range(0x4E00, 0x9FA5 + 1):
            char = chr(code)
            py_list = pinyin(char, style=Style.NORMAL)
            
            # 过滤掉转换失败的、没有拼音的罕见字
            if py_list and len(py_list[0]) > 0:
                py = py_list[0][0]
                if py != char:  # 如果拼音不等于原字，说明转换成功
                    f.write(f'    {{"{char}", "{py}"}},\n')
                    count += 1

        f.write("};\n")

    print(f"✅ 成功生成 zh_pinyin_map.h！")
    print(f"📊 共收录了 {count} 个汉字及其拼音（包含所有常用字和绝大多数生僻字）。")

if __name__ == "__main__":
    generate_full_map()