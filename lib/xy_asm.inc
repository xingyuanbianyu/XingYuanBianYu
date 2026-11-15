#pragma once
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include <iomanip>

enum ExtraOpCode {
    OP_MOV_IMM = 0x40, OP_MOV = 0x41, OP_MOV_F = 0x42, OP_MOV_S = 0x43,
    OP_ALLOC = 0x50, OP_REF = 0x51, OP_FREE = 0x52, OP_DEREF = 0x53,
    OP_H_MOV_IMM = 0x54, OP_H_MOV = 0x55, OP_H_MOV_F = 0x56, OP_H_MOV_S = 0x57
};

struct AssemblerInstruction {
    std::string op;
    std::vector<std::string> args;
};

class Assembler {
private:
    std::vector<AssemblerInstruction> instructions;
    std::vector<uint8_t> output;
    std::string mode; 

    uint8_t parseReg(const std::string& s) {
        if (s.empty() || (s[0] != 'r' && s[0] != 'R')) return 0;
        return (uint8_t)std::stoi(s.substr(1));
    }
    int parseInt(const std::string& s) { 
        try { return std::stoi(s, nullptr, 0); } catch(...) { return 0; }
    }
    double parseDouble(const std::string& s) { return std::stod(s); }

    std::vector<std::string> splitArgs(const std::string& line) {
        std::vector<std::string> args;
        std::string cur;
        bool in_str = false;
        for (char c : line) {
            if (c == '"') in_str = !in_str;
            if (c == ',' && !in_str) {
                args.push_back(cur); cur.clear();
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) args.push_back(cur);
        for (auto& s : args) {
            s.erase(0, s.find_first_not_of(" \t"));
            s.erase(s.find_last_not_of(" \t") + 1);
        }
        return args;
    }

    void emit8(uint8_t v) { output.push_back(v); }
    void emit16(uint16_t v) { output.push_back(v & 0xFF); output.push_back((v >> 8) & 0xFF); }
    void emit32(uint32_t v) { for(int i=0; i<4; ++i) output.push_back((v >> (i*8)) & 0xFF); }
    void emitDouble(double v) {
        uint8_t buf[8]; std::memcpy(buf, &v, 8);
        for (int i = 0; i < 8; ++i) output.push_back(buf[i]);
    }
    void emitString(const std::string& s) {
        std::string clean = s;
        if (clean.size() >= 2 && clean.front() == '"' && clean.back() == '"')
            clean = clean.substr(1, clean.size() - 2);
        emit16(clean.size());
        for (char c : clean) output.push_back(c);
    }

public:
    Assembler(std::string m) : mode(m) {}

    void parseFile(const std::string& filename) {
        std::ifstream ifs(filename);
        if (!ifs) throw std::runtime_error("无法打开文件: " + filename);

        std::string line;
        while (std::getline(ifs, line)) {
            size_t comment = line.find(';');
            if (comment != std::string::npos) line = line.substr(0, comment);
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            if (line.empty()) continue;

            AssemblerInstruction inst;
            size_t space = line.find_first_of(" \t");
            if (space == std::string::npos) {
                inst.op = line;
            } else {
                inst.op = line.substr(0, space);
                inst.args = splitArgs(line.substr(space + 1));
            }
            std::transform(inst.op.begin(), inst.op.end(), inst.op.begin(), ::toupper);
            instructions.push_back(inst);
        }
    }

    void generate() {
        for (auto& inst : instructions) {
            if (mode == "v") genStack(inst);
            else genReg(inst);
        }
    }

    // 🚀 唯一对外接口：把编译好的纯净字节流交出去
    std::vector<uint8_t> getOutput() const { return output; }

    void genStack(const AssemblerInstruction& inst) {
        if (inst.op == "PUSH") { emit8(0x00); emit32(parseInt(inst.args[0])); }
        else if (inst.op == "PUSH_F") { emit8(0x1B); emitDouble(parseDouble(inst.args[0])); }
        else if (inst.op == "PUSH_S") { emit8(0x1C); emitString(inst.args[0]); }
        else if (inst.op == "POP") { emit8(0x01); }
        else if (inst.op == "ADD") { emit8(0x04); }
        else if (inst.op == "SUB") { emit8(0x05); }
        else if (inst.op == "MUL") { emit8(0x06); }
        else if (inst.op == "DIV") { emit8(0x07); }
        else if (inst.op == "LOAD") { emit8(0x0A); emit8(inst.args[0][0]); }
        else if (inst.op == "STORE") { emit8(0x0B); emit8(inst.args[0][0]); }
        else if (inst.op == "JMP") { emit8(0x08); emit32(parseInt(inst.args[0])); }
        else if (inst.op == "JUMP_IF_FALSE") { emit8(0x0C); emit32(parseInt(inst.args[0])); }
        else if (inst.op == "PRINT") { emit8(0x02); }
        else if (inst.op == "PRINT_NO_NL") { emit8(0x24); }
        else if (inst.op == "HALT") { emit8(0x0F); }
        else if (inst.op == "LESS") { emit8(0x10); }
        else if (inst.op == "GREATER") { emit8(0x11); }
        else if (inst.op == "EQUAL") { emit8(0x12); }
        else if (inst.op == "INPUT") { emit8(0x13); }
        else if (inst.op == "OR") { emit8(0x14); }
        else if (inst.op == "AND") { emit8(0x15); }
        else if (inst.op == "NOT") { emit8(0x20); }
        else if (inst.op == "IDIV") { emit8(0x1E); }
        else if (inst.op == "POW") { emit8(0x1F); }
    }

    void genReg(const AssemblerInstruction& inst) {
        auto getOp = [&](const std::string& name) -> int {
            if (name == "MOV_IMM") return OP_MOV_IMM;
            if (name == "MOV") return OP_MOV;
            if (name == "MOV_F") return OP_MOV_F;
            if (name == "MOV_S") return OP_MOV_S;
            if (name == "H_MOV_IMM") return OP_H_MOV_IMM;
            if (name == "H_MOV") return OP_H_MOV;
            if (name == "H_MOV_F") return OP_H_MOV_F;
            if (name == "H_MOV_S") return OP_H_MOV_S;
            if (name == "ALLOC") return OP_ALLOC;
            if (name == "REF") return OP_REF;
            if (name == "FREE") return OP_FREE;
            if (name == "DEREF") return OP_DEREF;
            return -1;
        };

        int extOp = getOp(inst.op);
        if (extOp != -1) {
            emit8(extOp);
            if (inst.op == "MOV_IMM" || inst.op == "H_MOV_IMM") {
                emit8(parseReg(inst.args[0])); emit8(parseInt(inst.args[1]));
            } else if (inst.op == "MOV" || inst.op == "H_MOV") {
                emit8(parseReg(inst.args[0])); emit8(parseReg(inst.args[1]));
            } else if (inst.op == "MOV_F" || inst.op == "H_MOV_F") {
                emit8(parseReg(inst.args[0])); emitDouble(parseDouble(inst.args[1])); emit8(0);
            } else if (inst.op == "MOV_S" || inst.op == "H_MOV_S") {
                emit8(parseReg(inst.args[0])); emitString(inst.args[1]);
            } else if (inst.op == "ALLOC" || inst.op == "DEREF") {
                emit8(parseReg(inst.args[0])); emit8(parseReg(inst.args[1]));
            } else if (inst.op == "REF" || inst.op == "FREE") {
                emit8(parseReg(inst.args[0]));
            }
            return;
        }

        if (inst.op == "ADD" || inst.op == "SUB" || inst.op == "MUL" || inst.op == "DIV" || 
            inst.op == "IDIV" || inst.op == "POW" || inst.op == "LESS" || inst.op == "GREATER" || 
            inst.op == "EQUAL" || inst.op == "AND" || inst.op == "OR") {
            emit8(inst.op == "ADD" ? 0x04 : inst.op == "SUB" ? 0x05 : inst.op == "MUL" ? 0x06 : inst.op == "DIV" ? 0x07 : 
                  inst.op == "IDIV" ? 0x1E : inst.op == "POW" ? 0x1F : inst.op == "LESS" ? 0x10 : inst.op == "GREATER" ? 0x11 : 
                  inst.op == "EQUAL" ? 0x12 : inst.op == "AND" ? 0x15 : 0x14);
            emit8(parseReg(inst.args[0])); emit8(parseReg(inst.args[1])); emit8(parseReg(inst.args[2]));
        } else if (inst.op == "NOT") {
            emit8(0x20); emit8(parseReg(inst.args[0])); emit8(parseReg(inst.args[1]));
        } else if (inst.op == "LOAD") {
            emit8(0x0A); emit8(parseReg(inst.args[0])); emit8(inst.args[1][0]);
        } else if (inst.op == "STORE") {
            emit8(0x0B); emit8(inst.args[0][0]); emit8(parseReg(inst.args[1]));
        } else if (inst.op == "JMP" || inst.op == "CALL") {
            emit8(inst.op == "JMP" ? 0x08 : 0x21); 
            emit16(parseInt(inst.args[0])); 
        } else if (inst.op == "JUMP_IF_FALSE") {
            emit8(0x0C); emit8(parseReg(inst.args[0])); 
            emit16(parseInt(inst.args[1])); 
        } else if (inst.op == "PRINT" || inst.op == "PRINT_NO_NL" || inst.op == "INPUT" || inst.op == "TRUE" || inst.op == "FALSE") {
            emit8(inst.op == "PRINT" ? 0x02 : inst.op == "PRINT_NO_NL" ? 0x24 : inst.op == "INPUT" ? 0x13 : inst.op == "TRUE" ? 0x17 : 0x18);
            emit8(parseReg(inst.args[0]));
        } else if (inst.op == "HALT") {
            emit8(0x0F);
        } else if (inst.op == "RET") {
            emit8(0x22);
        } else if (inst.op == "NOP") {
            emit8(0x0E);
        }
    }
};