#pragma once
#include <string>
#include <vector>    // 🚀 必须加上，用于 vector
#include <cstdint>   // 🚀 必须加上，用于 uint8_t

enum class PathMode { Auto, Real, VFS };

// ============================================================
// Player —— 用户态播放器
//   只持有一个 handle，通过 SoundCard 通信。
//   纯内存流模式，不再依赖临时文件。
// ============================================================
class Player {
public:
    static Player& getInstance();

    std::string load(const std::string& path, PathMode mode = PathMode::Auto);

    bool play();
    bool pause();
    bool resume();
    bool stop();
    bool is_playing() const;
    bool is_paused() const;

    void set_volume(int v);
    int  get_volume() const;

    std::string info() const;

private:
    Player() = default;
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    int m_handle = -1;
    bool m_paused = false;
    std::string m_file;
    std::string m_play_path; // 保留以防后续兼容外部路径播放
    PathMode m_mode = PathMode::Auto;

    // 🚀 核心：存放从 VFS 读出来的纯二进制音频流
    std::vector<uint8_t> m_audio_buffer; 
};