#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <stack>
#include <map>
#include <cctype>

struct DebugError {
    int line;
    std::string msg;
};

class Debugger {
private:
    std::vector<std::string> m_lines;
    std::vector<DebugError> m_errors;
    std::stack<int> m_brace_stack;

public:
    bool load(const std::string& filename) {
        m_lines.clear();
        std::ifstream infile(filename);
        if (!infile.is_open()) {
            std::cerr << "[调试器] 无法打开: " << filename << "\n";
            return false;
        }
        std::string line;
        while (std::getline(infile, line)) {
            m_lines.push_back(line);
        }
        infile.close();
        return true;
    }

    bool runAllChecks() {
        m_errors.clear();
        checkBraceBalance();
        checkNodeStructure();
        checkPointerValidity();
        checkVariableConsistency();
        return m_errors.empty();
    }

    void printErrors() const {
        if (m_errors.empty()) {
            std::cout << "[调试器] ✅ 未发现任何错误！\n";
            return;
        }
        std::cout << "[调试器] ❌ 发现 " << m_errors.size() << " 个问题:\n";
        for (const auto& err : m_errors) {
            std::cout << "  行 " << err.line << ": " << err.msg << "\n";
        }
    }

private:
    // ============================================================
    // 花括号平衡：只扫描 MEID: 之前的部分
    //   —— 因为节点行会带 "}MEID:LID:N" 结尾，这个 } 是配对用
    //      KID:DEF/ID:{...} 的闭合符号，不能被忽略，
    //      但也不能把它当成"额外"的右花括号。
    //   —— 同时 DEF 行本身末尾的 "{" 也要统计。
    // 做法：把整行去掉尾部 "MEID:xxx" 后再数花括号。
    // ============================================================
    void checkBraceBalance() {
        m_brace_stack = std::stack<int>();
        for (size_t i = 0; i < m_lines.size(); ++i) {
            std::string line = m_lines[i];
            int lineNum = i + 1;

            // 截断 MEID: 之后的内容
            size_t cut = line.find("MEID:");
            std::string scan = (cut == std::string::npos)
                             ? line
                             : line.substr(0, cut);

            for (char c : scan) {
                if (c == '{') m_brace_stack.push(lineNum);
                else if (c == '}') {
                    if (m_brace_stack.empty()) {
                        m_errors.push_back({lineNum, "多余的右大括号 }"});
                    } else {
                        m_brace_stack.pop();
                    }
                }
            }
        }
        while (!m_brace_stack.empty()) {
            m_errors.push_back({m_brace_stack.top(), "大括号未闭合 }"});
            m_brace_stack.pop();
        }
    }

    // ============================================================
    // 节点结构检查：放行 KID:DEF / KID:RETURN / FID:CALL
    // ============================================================
    void checkNodeStructure() {
        for (size_t i = 0; i < m_lines.size(); ++i) {
            std::string line = m_lines[i];
            int lineNum = i + 1;
            std::string trimLine = trim(line);

            if (trimLine == "{" || trimLine == "}") continue;

            bool is_def    = trimLine.find("KID:DEF")    != std::string::npos;
            bool is_return = trimLine.find("KID:RETURN") != std::string::npos;
            bool is_call   = trimLine.find("FID:CALL")   != std::string::npos;

            if (is_def) {
                // DEF 行本身不带 ID:{ / MEID:LID:
                continue;
            }
            if (is_return || is_call) {
                if (trimLine.find("MEID:LID:") == std::string::npos) {
                    m_errors.push_back({lineNum, "逻辑节点缺少 'MEID:LID:'"});
                }
                continue;
            }

            if (trimLine.find("ID:{") == std::string::npos) {
                m_errors.push_back({lineNum, "缺少 'ID:{' 声明"});
            }
            if (trimLine.find("CHID:") == std::string::npos &&
                trimLine.find("MEID:LID:") == std::string::npos) {
                m_errors.push_back({lineNum, "逻辑节点缺少 'MEID:LID:'"});
            }
        }
    }

    // 检查 PID 语法合法性
    void checkPointerValidity() {
        for (size_t i = 0; i < m_lines.size(); ++i) {
            std::string line = m_lines[i];
            size_t pos = line.find("PID:0x");
            while (pos != std::string::npos) {
                size_t start = pos + 6;
                size_t end = start;
                while (end < line.size() && isxdigit((unsigned char)line[end])) end++;
                std::string pid = line.substr(start, end - start);

                if (pid.empty()) {
                    m_errors.push_back({(int)i + 1, "PID 缺少指针值 (PID:0x 后面为空)"});
                }
                for (char c : pid) {
                    if (!isxdigit((unsigned char)c)) {
                        m_errors.push_back({(int)i + 1, "PID 包含非法十六进制字符: " + pid});
                        break;
                    }
                }
                pos = line.find("PID:0x", end);
            }
        }
    }

    // 检查同一个变量（VID）是否一直被分配同一个指针
    void checkVariableConsistency() {
        std::map<std::string, std::string> varPidMap;

        for (size_t i = 0; i < m_lines.size(); ++i) {
            std::string line = m_lines[i];
            size_t pos = line.find("VID:");
            while (pos != std::string::npos) {
                size_t start = pos + 4;
                size_t end = start;
                while (end < line.size() && (isalnum((unsigned char)line[end]) || line[end] == '_')) end++;
                std::string varName = line.substr(start, end - start);

                size_t pidPos = line.rfind("PID:0x", pos);
                if (pidPos != std::string::npos && pidPos < pos) {
                    size_t pidStart = pidPos + 6;
                    size_t pidEnd = pidStart;
                    while (pidEnd < line.size() && isxdigit((unsigned char)line[pidEnd])) pidEnd++;
                    std::string pid = line.substr(pidStart, pidEnd - pidStart);

                    if (varPidMap.find(varName) == varPidMap.end()) {
                        varPidMap[varName] = pid;
                    } else if (varPidMap[varName] != pid) {
                        m_errors.push_back({(int)i + 1,
                            "变量 [" + varName + "] 指针冲突 (首次: 0x" +
                            varPidMap[varName] + ", 本次: 0x" + pid + ")"});
                    }
                }
                pos = line.find("VID:", end);
            }
        }
    }

    static std::string trim(const std::string& s) {
        size_t first = s.find_first_not_of(" \t");
        if (std::string::npos == first) return "";
        size_t last = s.find_last_not_of(" \t");
        return s.substr(first, (last - first + 1));
    }
};