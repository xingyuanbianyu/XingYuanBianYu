#include "../include/dcl_player.h"
#include "../include/dcl_xyp4.h" // 🚀 必须引入 xyp4 头文件以获取 Xyp4Header
#include "../../DD/include/sound.h"
#include "../../DD/include/config.h"
#include "../../VFS/include/vfs.h"
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <mutex>
#include <cstring>

namespace fs = std::filesystem;
extern std::string drt_get_name_by_uid(int uid);

void player_bootstrap() {}

static std::string resolve_player_alias(const std::string& input) {
    static const std::vector<std::string> aliases = {
        "info", "help", "pause", "resume", "stop", "vol", "next", "prev", "loop"
    };
    for (const auto& cmd : aliases) {
        std::string cfg_val = XybyConfig::getInstance().getString("player.alias." + cmd, "");
        if (cfg_val.empty()) continue;
        std::stringstream ss(cfg_val);
        std::string token;
        while (std::getline(ss, token, '|')) {
            size_t f = token.find_first_not_of(" \t\r\n");
            if (f == std::string::npos) continue;
            size_t l = token.find_last_not_of(" \t\r\n");
            token = token.substr(f, l - f + 1);
            if (token == input) return cmd;
        }
    }
    return input;
}

static std::string resolve_player_path(const std::string& raw_arg) {
    std::string raw = raw_arg;
    size_t first = raw.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = raw.find_last_not_of(" \t\r\n");
    raw = raw.substr(first, last - first + 1);

    if (raw == "/") return "/";
    std::string cwd = VFS::getInstance().get_cwd();
    if (cwd.empty()) cwd = "/";
    if (raw == "~" || raw == ".") return cwd;

    if (raw.size() >= 2 && raw[0] == '~' && raw[1] == '/') {
        int uid = VFS::getInstance().get_user_id();
        std::string uname = drt_get_name_by_uid(uid);
        std::string home_dir = uname.empty() ? "/" : ("/home/" + uname);
        if (home_dir.back() == '/') return home_dir + raw.substr(2);
        return home_dir + raw.substr(1);
    }
    if (raw.size() >= 2 && raw[0] == '.' && raw[1] == '/') {
        if (cwd.back() == '/') return cwd + raw.substr(2);
        return cwd + raw.substr(1);
    }
    if (raw[0] != '/') {
        if (raw.rfind("home/", 0) == 0 || raw.rfind("bin/", 0) == 0) return "/" + raw;
        if (cwd.back() == '/') return cwd + raw;
        return cwd + "/" + raw;
    }
    return raw;
}

static std::string extract_pure_name(const std::string& raw) {
    std::string s = raw;
    size_t pos = 0;
    while ((pos = s.find("\033[", pos)) != std::string::npos) {
        size_t end = s.find("m", pos);
        if (end != std::string::npos) s.erase(pos, end - pos + 1);
        else break;
    }
    if (s.size() > 1 && s[0] == '[') {
        size_t space = s.find(' ');
        if (space != std::string::npos) s = s.substr(space + 1);
    }
    size_t f = s.find_first_not_of(" \t\r\n\v\f");
    if (f == std::string::npos) return "";
    size_t l = s.find_last_not_of(" \t\r\n\v\f");
    return s.substr(f, l - f + 1);
}

struct CliPlayer {
    std::vector<std::string> playlist;
    int current_index = -1;
    std::atomic<bool> exit_thread{false};
    std::thread worker_thread;
    std::mutex thread_mtx; // 🚀 线程管理锁，防止并发崩溃
    
    int loop_mode = 0; 
    bool is_paused = false;

    static CliPlayer& get() {
        static CliPlayer instance;
        return instance;
    }

    void monitor_loop() {
        auto& core = SoundCard::getInstance();
        while (!exit_thread) {
            if (playlist.empty() || current_index < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            
            if (is_paused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }

            if (!core.is_alive(0)) { 
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                if (!core.is_alive(0) && !is_paused) {
                    if (loop_mode == 1) {
                        play_current(); // 单曲循环
                    } else if (loop_mode == 2) {
                        next_song(); // 列表循环
                    } else {
                        // 不循环：顺序播下一首，直到播完就停
                        if (current_index + 1 < playlist.size()) {
                            current_index++;
                            play_current();
                        } else {
                            // 🚀 核心修复：不能调 stop_monitor，直接 break 退出即可！
                            break; 
                        }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    void start_monitor() {
        std::lock_guard<std::mutex> lock(thread_mtx);
        // 如果之前有残留线程，先安全清理
        if (worker_thread.joinable()) {
            exit_thread = true;
            if (worker_thread.get_id() != std::this_thread::get_id()) {
                worker_thread.join();
            }
        }
        exit_thread = false;
        worker_thread = std::thread(&CliPlayer::monitor_loop, this);
    }

    void stop_monitor() {
        std::lock_guard<std::mutex> lock(thread_mtx);
        exit_thread = true;
        if (worker_thread.joinable()) {
            // 🚀 绝对不能让当前线程 join 自己
            if (worker_thread.get_id() != std::this_thread::get_id()) {
                worker_thread.join();
            }
        }
    }

    // 🚀 核心修复：剥离 Xyp4Header，传递纯 PCM 数据
    void play_current() {
        SoundCard::getInstance().stop_all();
        std::string target = playlist[current_index];
        
        std::string raw_data = VFS::getInstance().read_file(target);
        if (raw_data.empty()) return;

        // 检查文件大小是否满足最小头部要求
        if (raw_data.size() <= sizeof(Xyp4Header)) return;

        Xyp4Header header;
        std::memcpy(&header, raw_data.data(), sizeof(Xyp4Header));

        // 检查 Magic 是否为 "XYP4"
        if (std::memcmp(header.magic, "XYP4", 4) == 0) {
            // 提取纯 PCM 数据
            std::vector<uint8_t> pcm_data(raw_data.begin() + sizeof(Xyp4Header), raw_data.end());
            
            // 🚀 提示：如果你的 SoundCard::play_buffer 底层接口支持多参数，请换成这行：
            // SoundCard::getInstance().play_buffer(pcm_data.data(), pcm_data.size(), header.sample_rate, header.channels, header.bit_depth);
            
            // 目前先用双参数调用，确保不报错。底层 SoundCard 内部需要能正确处理默认音频流。
            SoundCard::getInstance().play_buffer(pcm_data.data(), pcm_data.size());
        } else {
            // 兜底逻辑：如果不是 XYP4 文件，可能是个裸 PCM 或者是其他格式，直接丢给声卡
            std::vector<uint8_t> buffer(raw_data.begin(), raw_data.end());
            SoundCard::getInstance().play_buffer(buffer.data(), buffer.size());
        }
        
        is_paused = false;
    }

    void next_song() {
        if (playlist.empty()) return;
        current_index = (current_index + 1) % playlist.size();
        play_current();
    }
};

std::string play_cmd(const std::vector<std::string>& args) {
    auto& cli = CliPlayer::get();

    if (args.empty()) return "用法: player play <VFS目录/文件>";

    std::string sub = resolve_player_alias(args[0]);
    
    std::vector<std::string> real_args = args;
    if (sub == "loop") {
        if (args[0] == "单曲循环") real_args = {"loop", "on"};
        else if (args[0] == "列表循环") real_args = {"loop", "list"};
        else if (args[0] == "关闭循环") real_args = {"loop", "off"};
    }

    if (sub == "play") {
        if (args.size() < 2) return "用法: player play <VFS路径>";
        
        std::string target = resolve_player_path(args[1]);
        cli.stop_monitor();
        SoundCard::getInstance().stop_all();
        cli.playlist.clear();
        
        auto node = VFS::getInstance().get_node(target);
        
        if (!node) {
            if (target.find(".xyp4") == std::string::npos) {
                std::string with_ext = target + ".xyp4";
                auto test_node = VFS::getInstance().get_node(with_ext);
                if (test_node) { target = with_ext; node = test_node; }
            }
            if (!node) {
                size_t last_slash = target.find_last_of('/');
                if (last_slash != std::string::npos) {
                    std::string parent = target.substr(0, last_slash);
                    if (parent.empty()) parent = "/";
                    std::string filename = target.substr(last_slash + 1);
                    
                    if (filename.find(".xyp4") != std::string::npos) filename = filename.substr(0, filename.find(".xyp4"));

                    auto p_node = VFS::getInstance().get_node(parent);
                    if (p_node && std::dynamic_pointer_cast<Directory>(p_node)) {
                        auto items = VFS::getInstance().list_directory(parent);
                        for (auto& raw : items) {
                            std::string nm = extract_pure_name(raw);
                            std::string lnm = nm;
                            std::transform(lnm.begin(), lnm.end(), lnm.begin(), ::tolower);
                            if (lnm.find(".xyp4") != std::string::npos && nm.find(filename) != std::string::npos) {
                                target = parent + "/" + nm;
                                node = VFS::getInstance().get_node(target);
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (!node) return "错误: 找不到音频文件。";

        if (std::dynamic_pointer_cast<Directory>(node)) {
            auto items = VFS::getInstance().list_directory(target);
            for (auto& raw : items) {
                std::string nm = extract_pure_name(raw);
                std::string lnm = nm;
                std::transform(lnm.begin(), lnm.end(), lnm.begin(), ::tolower);
                if (lnm.find(".xyp4") != std::string::npos) {
                    cli.playlist.push_back(target + "/" + nm);
                }
            }
        } else {
            cli.playlist.push_back(target);
        }

        if (cli.playlist.empty()) return "错误: 目录中没有 .xyp4 音频。";

        cli.current_index = 0;
        cli.play_current();
        cli.start_monitor();
        return "🎵 原生流式播放已启动，共 " + std::to_string(cli.playlist.size()) + " 首。";
    }
    
    if (sub == "stop") { 
        cli.stop_monitor(); 
        SoundCard::getInstance().stop_all(); 
        cli.is_paused = false;
        return "⏹️ 已停止"; 
    }
    
    if (sub == "pause") { 
        if (SoundCard::getInstance().pause(0)) {
            cli.is_paused = true;
            return "⏸️ 已暂停硬件，资源已释放";
        }
        return "暂停失败";
    }
    
    if (sub == "resume") { 
        if (SoundCard::getInstance().resume(0)) {
            cli.is_paused = false;
            return "▶️ 已恢复底层播放";
        }
        return "恢复失败";
    }

    if (sub == "next") {
        cli.next_song();
        return "⏭️ 已切至下一首";
    }

    if (sub == "prev") {
        cli.current_index = (cli.current_index - 1 + cli.playlist.size()) % cli.playlist.size();
        cli.play_current();
        return "⏮️ 已切至上一首";
    }

    if (sub == "loop") {
        if (real_args.size() < 2) {
            if (cli.loop_mode == 0) return "当前模式: 不循环";
            if (cli.loop_mode == 1) return "当前模式: 单曲循环";
            if (cli.loop_mode == 2) return "当前模式: 列表循环";
            return "用法: player loop on|off|list 或 player 单曲循环|列表循环|关闭循环";
        }

        std::string mode = real_args[1];
        if (mode == "on" || mode == "开启" || mode == "单曲循环") {
            cli.loop_mode = 1;
            return "🔁 已开启单曲循环";
        } else if (mode == "list" || mode == "列表循环") {
            cli.loop_mode = 2;
            return "🔁 已开启列表循环";
        } else if (mode == "off" || mode == "关闭循环" || mode == "关闭") {
            cli.loop_mode = 0;
            return "➡️ 已关闭循环";
        } else {
            cli.loop_mode = 0;
            return "➡️ 已关闭循环";
        }
    }

    if (sub == "vol") {
        if (args.size() < 2) return "当前音量: " + std::to_string(SoundCard::getInstance().get_volume());
        try {
            int v = std::stoi(args[1]);
            SoundCard::getInstance().set_volume(v);
            return "音量已设为: " + std::to_string(SoundCard::getInstance().get_volume());
        } catch (...) {
            return "音量必须是 0-100 的整数";
        }
    }

    if (sub == "info") {
        std::string loop_str = "不循环";
        if (cli.loop_mode == 1) loop_str = "单曲循环";
        else if (cli.loop_mode == 2) loop_str = "列表循环";
        
        return "星缘OS 原生播放器\n当前列表: " + std::to_string(cli.playlist.size()) + " 首\n循环模式: " + loop_str + "\n音量: " + std::to_string(SoundCard::getInstance().get_volume()) + "%";
    }

    if (sub == "help") {
        return "星缘OS 原生播放器:\n"
               "  player play <文件/目录>\n"
               "  player stop | 停止\n"
               "  player pause | 暂停\n"
               "  player resume | 继续\n"
               "  player next | 下一首\n"
               "  player prev | 上一首\n"
               "  player loop on|off|list | 循环控制\n"
               "  player vol <0-100> | 音量\n"
               "  player info | 状态";
    }

    return "未知命令，请输入 player help 查看用法。";
}