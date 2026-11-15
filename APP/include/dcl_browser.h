#pragma once
#include <string>

std::string render_xyip_page(const std::string& content);

// 【核心修改】返回值语义：
//   true  = 内容已在真实窗口（GUI）里渲染，命令行不要再输出内容
//   false = 降级到终端，内容已经在终端里渲染过了，命令行不要再输出内容
bool render_and_show(const std::string& content);