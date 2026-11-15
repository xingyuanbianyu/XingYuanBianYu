#include "../include/player.h"
#include "../include/sound.h"
#include "../../VFS/include/vfs.h"
#include <iostream>
#include <vector>
#include <string>

Player& Player::getInstance() {
    static Player instance;
    return instance;
}

Player::~Player() { stop(); }

// ============================================================
// 🚀 核心改造：直接从 VFS 读进内存，不再落盘
// ============================================================
std::string Player::load(const std::string& path, PathMode mode) {
    if (path.empty()) return "未指定文件";
    stop(); // 先停旧的

    m_file = path;

    if (mode == PathMode::VFS || mode == PathMode::Auto) {
        std::string norm = VFS::getInstance().normalize_path(path);
        auto node = VFS::getInstance().get_node(norm);
        
        if (node) {
            // 🚀 直接读取为纯文本/二进制流，保存在内存里
            std::string raw_data = VFS::getInstance().read_file(norm);
            if (raw_data.empty()) return "读取 VFS 文件内容为空: " + norm;
            
            // 转存到全局的内存 Buffer 中
            m_audio_buffer.assign(raw_data.begin(), raw_data.end());
            return "";
        } else if (mode == PathMode::VFS) {
            return "文件不存在 (VFS): " + norm;
        }
    }

    return "暂不支持从宿主机直接加载（纯原生模式）";
}

// ============================================================
// 🚀 核心改造：播放内存流
// ============================================================
bool Player::play() {
    if (m_audio_buffer.empty()) return false;
    stop(); // 幂等

    // 🚀 关键：调用底层 SoundCard 的播放内存接口！
    // 如果你的底层 SoundCard 目前只有 play(路径)，你需要给它加上 play_buffer(const void*, size_t)
    m_handle = SoundCard::getInstance().play_buffer(m_audio_buffer.data(), m_audio_buffer.size());
    
    m_paused = false;
    return m_handle >= 0;
}

bool Player::pause() {
    if (m_handle < 0) return false;
    if (SoundCard::getInstance().pause(m_handle)) {
        m_paused = true;
        return true;
    }
    return false;
}

bool Player::resume() {
    if (m_handle < 0) return false;
    if (SoundCard::getInstance().resume(m_handle)) {
        m_paused = false;
        return true;
    }
    return false;
}

bool Player::stop() {
    if (m_handle >= 0) {
        SoundCard::getInstance().stop(m_handle);
        m_handle = -1;
    }
    m_paused = false;
    return true;
}

bool Player::is_playing() const {
    if (m_handle < 0) return false;
    return SoundCard::getInstance().is_alive(m_handle);
}

bool Player::is_paused() const {
    return m_handle >= 0 && m_paused;
}

void Player::set_volume(int v) {
    SoundCard::getInstance().set_volume(v);
}

int Player::get_volume() const {
    return SoundCard::getInstance().get_volume();
}

std::string Player::info() const {
    std::string s;
    s += "播放器状态:\n";
    s += "  文件: " + (m_file.empty() ? "(未加载)" : m_file) + "\n";
    s += "  内存占用: " + std::to_string(m_audio_buffer.size() / 1024) + " KB\n";
    s += "  音量: " + std::to_string(SoundCard::getInstance().get_volume()) + "\n";

    const char* st = "已停止";
    if (m_handle >= 0) st = m_paused ? "已暂停" : "播放中";
    s += "  状态: " + std::string(st);

    return s;
}