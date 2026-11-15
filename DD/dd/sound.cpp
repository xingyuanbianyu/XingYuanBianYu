#include "../include/sound.h"
#include "../include/config.h"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <cstring>
#include <algorithm>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

static ma_device g_device;
static ma_context g_context;
static bool g_device_inited = false; 
static std::atomic<bool> g_is_playing(false);
static std::atomic<bool> g_is_paused(false); // 🚀 暂停标记

static std::vector<int16_t> g_pcm_data;
static size_t g_pcm_offset = 0; 
static int g_sample_rate = 44100;
static int g_channels = 2;

static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    int16_t* out = (int16_t*)pOutput;
    size_t needed_samples = frameCount * g_channels;

    // 🚀 如果被标记为暂停，直接输出静音，并且彻底不读 PCM 数据
    if (g_is_paused) {
        memset(out, 0, needed_samples * sizeof(int16_t));
        return;
    }

    if (!g_is_playing || g_pcm_offset >= g_pcm_data.size()) {
        memset(out, 0, needed_samples * sizeof(int16_t));
        if (g_is_playing) g_is_playing = false;
        return;
    }

    size_t available = g_pcm_data.size() - g_pcm_offset;
    size_t to_copy = std::min(needed_samples, available);

    memcpy(out, g_pcm_data.data() + g_pcm_offset, to_copy * sizeof(int16_t));
    
    if (to_copy < needed_samples) {
        memset(out + to_copy, 0, (needed_samples - to_copy) * sizeof(int16_t));
    }
    
    g_pcm_offset += to_copy;
}

SoundCard& SoundCard::getInstance() {
    static SoundCard instance;
    return instance;
}

SoundCard::SoundCard() {}
SoundCard::~SoundCard() { stop_all(); }

bool SoundCard::is_ready() const { return m_ready; }
std::string SoundCard::get_backend() const { return "native_miniaudio"; }

void SoundCard::init() {
    if (m_ready) return;
    m_volume = XybyConfig::getInstance().getInt("sound.volume", 100);
    m_ready = true; 
}

int SoundCard::play_buffer(const void* data, size_t size) {
    if (!data || size < 16) return -1;
    stop_all(); 

    const uint8_t* ptr = (const uint8_t*)data;
    size_t pcm_start = 16; 

    g_sample_rate = *(uint32_t*)(ptr + 4);
    g_channels    = *(uint16_t*)(ptr + 8);

    size_t pcm_bytes = size - pcm_start;
    size_t total_samples = pcm_bytes / sizeof(int16_t);
    
    std::vector<int16_t> temp_pcm((int16_t*)(ptr + pcm_start), (int16_t*)(ptr + pcm_start) + total_samples);
    g_pcm_data.swap(temp_pcm);
    g_pcm_offset = 0;
    g_is_playing = true;
    g_is_paused = false;

    ma_backend backends[] = { ma_backend_opensl, ma_backend_aaudio };
    ma_result res = ma_context_init(backends, 2, NULL, &g_context);
    if (res != MA_SUCCESS) {
        res = ma_context_init(NULL, 0, NULL, &g_context);
        if (res != MA_SUCCESS) return -1;
    }

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_s16;
    config.playback.channels = g_channels;
    config.sampleRate        = g_sample_rate;
    config.dataCallback      = data_callback;
    
    res = ma_device_init(&g_context, &config, &g_device);
    if (res != MA_SUCCESS) {
        ma_context_uninit(&g_context);
        return -1;
    }
    g_device_inited = true;

    ma_device_start(&g_device); 
    return 1;
}

bool SoundCard::is_alive(int handle) const { 
    return g_is_playing;
}

void SoundCard::stop_all() {
    g_is_playing = false;
    g_is_paused = false;
    if (g_device_inited) {
        ma_device_uninit(&g_device);
        g_device_inited = false;
    }
    if (g_context.backend != ma_backend_null) {
        ma_context_uninit(&g_context);
        g_context.backend = ma_backend_null;
    }
    std::vector<int16_t>().swap(g_pcm_data);
}

bool SoundCard::stop(int handle) { stop_all(); return true; }

// 🚀 暂停：释放底层设备但保留上下文
bool SoundCard::pause(int handle) {
    if (!g_device_inited || !g_is_playing) return false;
    g_is_paused = true;
    ma_device_stop(&g_device); // 让设备休眠
    return true;
}

// 🚀 恢复：唤醒设备
bool SoundCard::resume(int handle) {
    if (!g_device_inited || !g_is_playing) return false;
    g_is_paused = false;
    ma_device_start(&g_device); // 重新启动
    return true;
}

void SoundCard::set_volume(int v) { 
    if (v < 0) v = 0; 
    if (v > 100) v = 100; 
    m_volume = v; 
    if (g_device_inited) ma_device_set_master_volume(&g_device, v / 100.0f);
}
int SoundCard::get_volume() const { return m_volume; }
bool SoundCard::beep() { return false; }
int SoundCard::play(const std::string& path) { return -1; }
int SoundCard::play_tone(double f, int ms, double v) { return -1; }