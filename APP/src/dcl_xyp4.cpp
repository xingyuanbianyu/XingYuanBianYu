#include "../include/dcl_xyp4.h"
#include "../../VFS/include/vfs.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <memory> // for unique_ptr

// 辅助：递归创建 VFS 目录
static bool vfs_ensure_dir(const std::string& vfs_path) {
    std::string norm = VFS::getInstance().normalize_path(vfs_path);
    if (norm.empty() || norm == "/") return true;
    std::stringstream ss(norm);
    std::string seg, cur = "";
    while (std::getline(ss, seg, '/')) {
        if (seg.empty()) continue;
        cur += "/" + seg;
        if (!VFS::getInstance().get_node(cur)) {
            VFS::getInstance().create_directory(cur);
        }
    }
    return true;
}

std::string xyp4_cmd(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return "用法: xyp4 <输入文件(MP3/WAV)> <输出文件.xyp4>";
    }

    std::string in_path = args[0];
    std::string out_path = args[1];

    // 1. 从 VFS 读取源文件
    auto node = VFS::getInstance().get_node(in_path);
    if (!node) return "错误: VFS 中找不到源文件 " + in_path;

    std::string raw_data = VFS::getInstance().read_file(in_path);
    if (raw_data.empty()) return "错误: 源文件为空或读取失败。";

    // 2. 将源文件转储到宿主机临时目录
    std::filesystem::create_directories(".xyby_tmp");
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::string unique_id = std::to_string(now) + "_" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    
    std::string temp_src = ".xyby_tmp/xyp4_src_" + unique_id + ".tmp";
    std::string temp_wav = ".xyby_tmp/xyp4_out_" + unique_id + ".wav";

    // 使用 RAII 确保临时文件一定会被清理（防止中间 return 导致垃圾残留）
    auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(temp_src, ec);
        std::filesystem::remove(temp_wav, ec);
    };

    std::ofstream ofs(temp_src, std::ios::binary);
    if (!ofs) { cleanup(); return "错误: 无法创建临时文件，请检查宿主机权限。"; }
    ofs.write(raw_data.data(), raw_data.size());
    ofs.close();

    // 3. 调用 ffmpeg (去掉 2>/dev/null 并强制 44 字节头)
    // -map_metadata -1 剥离元数据，确保 WAV 头是纯粹的 44 字节
    std::string cmd = "ffmpeg -y -i \"" + temp_src + "\" -f wav -acodec pcm_s16le -map_metadata -1 \"" + temp_wav + "\" 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) { cleanup(); return "错误: 无法执行 ffmpeg 命令。"; }
    
    char buffer[128];
    std::string ffmpeg_out = "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        ffmpeg_out += buffer;
    }
    int exit_code = pclose(pipe);

    if (exit_code != 0) {
        cleanup();
        return "错误: ffmpeg 转换失败。\nFFmpeg输出: " + ffmpeg_out.substr(0, 200) + "...\n请确保 Termux 中 ffmpeg 依赖完整。";
    }

    // 4. 读取生成的 WAV 文件
    std::ifstream ifs(temp_wav, std::ios::binary | std::ios::ate);
    if (!ifs) { cleanup(); return "错误: 无法读取临时 WAV 文件。"; }

    std::streamsize wav_size = ifs.tellg();
    if (wav_size <= 44) {
        cleanup();
        return "错误: 转换后的 WAV 文件过小 (" + std::to_string(wav_size) + " 字节)，提取 PCM 失败。";
    }

    // 解析 WAV 头（强制默认按 44 字节头读取）
    ifs.seekg(0, std::ios::beg);
    char wav_header[44];
    ifs.read(wav_header, 44);

    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;

    memcpy(&channels, wav_header + 22, 2);
    memcpy(&sample_rate, wav_header + 24, 4);
    memcpy(&bits_per_sample, wav_header + 34, 2);

    if (channels == 0) channels = 2;
    if (sample_rate == 0) sample_rate = 44100;
    if (bits_per_sample == 0) bits_per_sample = 16;

    ifs.seekg(44, std::ios::beg); 
    std::vector<char> pcm_data(wav_size - 44);
    ifs.read(pcm_data.data(), pcm_data.size());
    ifs.close();

    // 清理临时文件
    cleanup();

    if (pcm_data.empty()) {
        return "错误: 提取的 PCM 数据为空！";
    }

    // 5. 构造 XYP4
    Xyp4Header header;
    memcpy(header.magic, "XYP4", 4);
    header.sample_rate = sample_rate;
    header.channels = channels;
    header.bit_depth = bits_per_sample;
    
    // 防除零
    uint32_t block_align = channels * (bits_per_sample / 8);
    header.total_samples = (block_align > 0) ? (pcm_data.size() / block_align) : 0;

    std::string out_data;
    out_data.append((char*)&header, sizeof(header));
    out_data.append(pcm_data.data(), pcm_data.size());

    // 🚀 核心修复：自动创建输出路径的父目录
    size_t last_slash = out_path.find_last_of('/');
    if (last_slash != std::string::npos) {
        std::string parent = out_path.substr(0, last_slash);
        if (!parent.empty()) vfs_ensure_dir(parent);
    }

    auto out_node = VFS::getInstance().get_node(out_path);
    std::string write_err;
    bool is_new_file = false;

    if (out_node) {
        write_err = VFS::getInstance().write_file(out_path, out_data);
    } else {
        write_err = VFS::getInstance().create_file(out_path, out_data);
        is_new_file = true; // 🚀 标记这是新创建的文件
    }

    if (!write_err.empty()) {
        return "错误: 写入 VFS 失败 - " + write_err;
    }

    // 🚀 核心修复：新文件创建成功后，直接继承父目录的完整属性！
    if (is_new_file) {
        // 1. 提取父目录路径
        std::string parent_dir = "/";
        size_t pos = out_path.find_last_of('/');
        if (pos != std::string::npos) {
            parent_dir = out_path.substr(0, pos);
            if (parent_dir.empty()) parent_dir = "/";
        }

        // 2. 获取父目录（比如 ~/音乐）的真实资源属性
        ResourceInfo parent_info = VFS::getInstance().get_resource_info_dynamic(parent_dir);

        // 3. 构造新文件的属性
        ResourceInfo info;
        info.type = parent_info.type;           // 继承父目录类型 (私有资源)
        info.perms = parent_info.perms;         // 继承父目录权限 (读写改删清查执)
        info.domain_id = parent_info.domain_id; // 继承归属域
        info.group_id = parent_info.group_id;   // 继承组
        
        // 4. 唯一区别：归属者设为当前生成文件的用户
        info.owner_id = VFS::getInstance().get_effective_owner(); 

        // 5. 强行写入 VFS 元数据
        VFS::getInstance().set_resource_info(out_path, info);
    }

    double duration_sec = (sample_rate > 0) ? (double)header.total_samples / sample_rate : 0.0;

    return "🎉 转换成功！格式: " + std::to_string(sample_rate) + "Hz/" 
           + std::to_string(channels) + "声道, 总时长约 " 
           + std::to_string((int)duration_sec) + " 秒。";
}