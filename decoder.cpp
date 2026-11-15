// ============================================================
// decoder.cpp —— 独立解码器（全局层）
// 功能：读 .xye / .xya / .xyb 文件（24 字节头），按魔数分发
// 🚀 新增：完美接管前后端的 I/O 链接（stdin/stdout重定向 + 传入 FD）
// ============================================================
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <unistd.h>  // 🚀 用于 dup, dup2, close

// ============================================================
// 四个执行后端（🚀 均已修改为支持接收 io_fd）
// ============================================================
extern bool execute_bytecode(const std::string& code, bool trace, int io_fd = -1);          // 栈式
extern bool execute_bytecode_register(const std::string& code, bool trace, int io_fd = -1); // 寄存器式
extern bool execute_bytecode_heap(const std::string& code, bool trace, int io_fd = -1);     // 🚀 堆式
extern int  execute_xy_code(const std::string& code);                                       // 解释器

// ============================================================
// 文件头结构（24 字节）
// ============================================================
struct DecodedHeader {
    bool ok = false;
    std::string owner;       // 9 字节
    std::string version;     // 6 字节
    std::string magic;       // 5 字节
    uint32_t valid_bits = 0; // 3 字节 小端
    uint8_t group = 7;       // 1 字节
    std::string bits;        // 中间态 0/1 串
};

// ============================================================
// 🚀 核心：纯内存解码
// ============================================================
inline DecodedHeader decode_buffer(const std::string& raw,
                                   const std::string& expect_magic = "") {
    DecodedHeader h;

    if (raw.size() < 24) {
        std::cerr << "[DECODER] 数据小于 24 字节头 (实际 " << raw.size() << " 字节)\n";
        return h;
    }

    size_t pos = 0;

    char buf9[9] = {0};
    std::memcpy(buf9, raw.data() + pos, 9); pos += 9;
    h.owner = std::string(buf9, strnlen(buf9, 9));

    char buf6[6] = {0};
    std::memcpy(buf6, raw.data() + pos, 6); pos += 6;
    h.version = std::string(buf6, strnlen(buf6, 6));

    char buf5[5] = {0};
    std::memcpy(buf5, raw.data() + pos, 5); pos += 5;
    h.magic = std::string(buf5, strnlen(buf5, 5));

    if (!expect_magic.empty() && h.magic != expect_magic) {
        std::cerr << "[DECODER] 魔数不匹配: 期望 " << expect_magic
                  << " 实际 " << h.magic << "\n";
        return h;
    }

    uint8_t vb3[3];
    std::memcpy(vb3, raw.data() + pos, 3); pos += 3;
    h.valid_bits = (uint32_t)vb3[0] | ((uint32_t)vb3[1] << 8) | ((uint32_t)vb3[2] << 16);

    h.group = (uint8_t)raw[pos]; pos += 1;

    std::string bytes = raw.substr(pos);

    std::string bits;
    bits.reserve(bytes.size() * 8);
    for (char ch : bytes) {
        unsigned char b = (unsigned char)ch;
        for (int i = 7; i >= 0; --i) bits += ((b >> i) & 1) ? '1' : '0';
    }

    if (h.valid_bits > 0 && bits.size() > h.valid_bits) {
        bits.resize(h.valid_bits);
    }

    if (h.valid_bits > bits.size()) {
        std::cerr << "[DECODER] 警告: valid_bits(" << h.valid_bits
                  << ") 超过实际 bits(" << bits.size() << ")，已截断\n";
        h.valid_bits = (uint32_t)bits.size();
    }

    h.bits = std::move(bits);
    h.ok = true;
    return h;
}

inline DecodedHeader decode_file(const std::string& path,
                                 const std::string& expect_magic = "") {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "[DECODER] 无法打开: " << path << "\n";
        return DecodedHeader();
    }
    std::stringstream ss;
    ss << ifs.rdbuf();
    ifs.close();
    return decode_buffer(ss.str(), expect_magic);
}

inline std::string decode_bits_to_source(const std::string& bits) {
    std::string source;
    source.reserve(bits.size() / 7);
    for (size_t i = 0; i + 7 <= bits.size(); i += 7) {
        unsigned char ch = 0;
        for (int j = 0; j < 7; ++j) {
            ch = (unsigned char)((ch << 1) | (bits[i + j] - '0'));
        }
        source += (char)ch;
    }
    return source;
}

inline std::string decode_bits_to_bytes(const std::string& bits) {
    std::string bytes;
    bytes.reserve(bits.size() / 8 + 1);
    unsigned char b = 0;
    int n = 0;
    for (char c : bits) {
        b = (unsigned char)((b << 1) | (c == '1' ? 1 : 0));
        if (++n == 8) { bytes += (char)b; b = 0; n = 0; }
    }
    if (n > 0) { b <<= (8 - n); bytes += (char)b; }
    return bytes;
}

inline void decode_print_header(const DecodedHeader& h) {
    std::cerr << "=========================================\n";
    std::cerr << "归属:  " << h.owner << "\n";
    std::cerr << "版本:  " << h.version << "\n";
    std::cerr << "魔数:  " << h.magic << "\n";
    std::cerr << "位数:  " << h.valid_bits << "\n";
    std::cerr << "分组:  " << (int)h.group << "\n";
    std::cerr << "=========================================\n";
}

// ============================================================
// 🚀 终极分发器：支持直接绑定链接 (io_fd)
// ============================================================
int decode_and_run_memory(const std::string& raw_content, const std::string& name = "", int io_fd = -1) {
    // ============================================================
    // 🚀 【核心接管】如果传入了 io_fd，强行劫持底层的标准输入和输出
    // 这能兜住任何直接调用 printf 或 cout 的漏网之鱼
    // ============================================================
    int old_stdout = -1;
    int old_stderr = -1;
    int old_stdin = -1;

    if (io_fd != -1) {
        // 保存后台默认的输出入口
        old_stdout = dup(STDOUT_FILENO);
        old_stderr = dup(STDERR_FILENO);
        old_stdin = dup(STDIN_FILENO);

        // 把前台链接（Socket fd）接管过来！
        dup2(io_fd, STDOUT_FILENO);
        dup2(io_fd, STDERR_FILENO);
        dup2(io_fd, STDIN_FILENO); 
    }

    DecodedHeader h = decode_buffer(raw_content, "");
    if (!h.ok) {
        std::cerr << "[DECODER] 读取失败: " << name << "\n";
        if (io_fd != -1) { 
            dup2(old_stdout, STDOUT_FILENO); 
            dup2(old_stderr, STDERR_FILENO); 
            dup2(old_stdin, STDIN_FILENO); 
            close(old_stdout); close(old_stderr); close(old_stdin); 
        }
        return 1;
    }

    decode_print_header(h);

    int result = 1;

    // ===== 栈式虚拟机 =====
    if (h.magic == "XYBV" || h.magic == "XYVM") {
        std::cerr << "[DECODER] 类型: " << h.magic << " 字节码 → 栈式 VM\n";
        // 🚀 传递 io_fd 给虚拟机
        result = execute_bytecode(h.bits, false, io_fd) ? 0 : 1;
    }
    // ===== 寄存器式虚拟机 =====
    else if (h.magic == "XYBM") {
        std::cerr << "[DECODER] 类型: XYBM 字节码 → 寄存器式 VM\n";
        // 🚀 传递 io_fd 给虚拟机
        result = execute_bytecode_register(h.bits, false, io_fd) ? 0 : 1;
    }
    // ===== 🚀 堆式虚拟机 =====
    else if (h.magic == "XYBH") {
        std::cerr << "[DECODER] 类型: XYBH 字节码 → 堆式 VM\n";
        // 🚀 传递 io_fd 给虚拟机
        result = execute_bytecode_heap(h.bits, false, io_fd) ? 0 : 1;
    }
    // ===== 解释器（源码）=====
    else if (h.magic == "XYBY" || h.magic == "XYUT") {
        std::cerr << "[DECODER] 类型: " << h.magic << " → 源码 → 执行\n";
        std::string source = decode_bits_to_source(h.bits);
        result = execute_xy_code(source) == 0 ? 0 : 1;
    }
    // ===== 纯二进制 hex dump =====
    else if (h.magic == "XYEF") {
        std::cerr << "[DECODER] 类型: XYEF 纯二进制 → hex dump\n";
        std::string bytes = decode_bits_to_bytes(h.bits);
        for (size_t i = 0; i < bytes.size(); ++i) {
            printf("%02X ", (unsigned char)bytes[i]);
            if ((i + 1) % 16 == 0) std::cout << "\n";
        }
        if (bytes.size() % 16 != 0) std::cout << "\n";
        result = 0;
    } else {
        std::cerr << "[DECODER] 未知魔数: " << h.magic << "\n";
    }

    // ============================================================
    // 🚀 【恢复现场】虚拟机执行完毕，把链接还给后台
    // ============================================================
    if (io_fd != -1) {
        // 确保所有 C 和 C++ 的缓冲区都吐干净了，不然会丢字！
        std::cout.flush();
        std::cerr.flush();
        fflush(stdout);
        fflush(stderr);

        // 恢复后台默认的输入输出
        dup2(old_stdout, STDOUT_FILENO);
        dup2(old_stderr, STDERR_FILENO);
        dup2(old_stdin, STDIN_FILENO);

        close(old_stdout);
        close(old_stderr);
        close(old_stdin);
    }

    return result;
}

// ============================================================
// 兼容旧接口：从文件路径读取并执行
// ============================================================
inline int decode_and_run(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "[DECODER] 无法打开: " << path << "\n";
        return 1;
    }
    std::stringstream ss;
    ss << ifs.rdbuf();
    ifs.close();
    return decode_and_run_memory(ss.str(), path);
}