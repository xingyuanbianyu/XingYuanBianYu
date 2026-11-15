#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <mutex>
#include <sys/types.h>

struct SoundBuffer {
    uint32_t sample_rate     = 22050;
    uint16_t channels        = 1;
    uint16_t bits_per_sample = 16;
    std::vector<uint8_t> data;
};

class SoundCard {
public:
    static SoundCard& getInstance();

    void init();
    bool is_ready() const;
    std::string get_backend() const;

    int  play(const std::string& host_path);
    int  play_tone(double freq_hz, int duration_ms, double volume = 0.8);
    
    // 🚀 纯血原生：直接吃内存，不产生子进程
    int  play_buffer(const void* data, size_t size);

    bool is_alive(int handle) const;

    bool stop(int handle);
    void stop_all();
    bool pause(int handle);
    bool resume(int handle);

    void set_volume(int v);
    int  get_volume() const;
    bool beep();

private:
    SoundCard();
    ~SoundCard();
    SoundCard(const SoundCard&) = delete;
    SoundCard& operator=(const SoundCard&) = delete;

    std::string m_backend = "none";
    bool m_ready = false;
    int  m_volume = 100;
    int m_next_handle = 1;

    mutable std::recursive_mutex m_mutex;
};