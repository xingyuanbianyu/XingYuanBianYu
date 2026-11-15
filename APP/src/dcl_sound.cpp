#include "dcl_sound.h"
#include "../../DD/include/sound.h"
#include "../../DD/include/config.h"
#include "../../VFS/include/vfs.h"

#include <iostream>
#include <string>
#include <vector>

// ============================================================
// 🚀 声卡初始化（main.cpp 在 VFS::init 之后调用）
// ============================================================
void sound_bootstrap() {
    auto& sc  = SoundCard::getInstance();
    auto& cfg = XybyConfig::getInstance();
    if (!sc.is_ready()) sc.init();

    int v = cfg.getInt("sound.volume", 100);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    sc.set_volume(v);
}

// ============================================================
// sound 命令 (基础声卡测试)
// ============================================================
std::string sound_cmd(const std::vector<std::string>& args) {
    auto& sc  = SoundCard::getInstance();
    auto& cfg = XybyConfig::getInstance();
    if (!sc.is_ready()) sc.init();

    if (args.empty() || args[0] == "info") {
        return "声卡状态:\n"
               "  后端: " + sc.get_backend() + "\n"
               "  音量: " + std::to_string(sc.get_volume()) + "\n"
               "  就绪: " + (sc.is_ready() ? "是" : "否");
    }

    const std::string& sub = args[0];

    if (sub == "beep") {
        return sc.beep() ? "蜂鸣 OK" : "蜂鸣失败（后端未就绪）";
    }

    if (sub == "tone") {
        if (args.size() < 3) return "用法: sound tone <频率> <毫秒>";
        try {
            double freq = std::stod(args[1]);
            int    ms   = std::stoi(args[2]);
            int h = sc.play_tone(freq, ms);
            return h >= 0
                 ? ("已播放 " + args[1] + "Hz / " + args[2] + "ms (handle=" + std::to_string(h) + ")")
                 : "播放失败";
        } catch (...) {
            return "参数必须是数字";
        }
    }

    if (sub == "play") {
        if (args.size() < 2) return "用法: sound play <文件路径>";
        int h = sc.play(args[1]);
        return h >= 0
             ? ("已播放: " + args[1] + " (handle=" + std::to_string(h) + ")")
             : ("播放失败: " + args[1]);
    }

    if (sub == "vol") {
        if (args.size() < 2) return "用法: sound vol <0-100>";
        try {
            int v = std::stoi(args[1]);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            sc.set_volume(v);
            // 🚀 写回 XybyConfig（唯一配置中心）
            cfg.set("sound.volume", std::to_string(v));
            cfg.save();
            return "音量已设为 " + std::to_string(sc.get_volume());
        } catch (...) {
            return "音量必须是 0-100 的整数";
        }
    }

    if (sub == "stop") {
        sc.stop_all();
        return "已停止";
    }

    return "未知子命令: " + sub;
}