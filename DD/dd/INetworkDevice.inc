// ============================================================
// INetworkDevice.inc —— 跨平台网卡抽象层
// ============================================================
#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <memory>

// 最大传输单元 (通常以太网是 1500)
constexpr size_t MTU = 1500;

// 网络数据包（纯字节流，不带任何硬件色彩）
struct NetPacket {
    std::vector<uint8_t> data; // 实际数据（包括以太网帧头）
    uint32_t len;              // 数据长度

    NetPacket() : len(0) {}
    NetPacket(const uint8_t* buf, uint32_t l) : data(buf, buf + l), len(l) {}
};

// ============================================================
// 【核心接口】网卡设备
// 所有平台的驱动都必须实现这个接口
// ============================================================
class INetworkDevice {
public:
    virtual ~INetworkDevice() = default;

    // 初始化网卡（重置寄存器、分配 DMA 缓冲区等）
    virtual bool init() = 0;

    // 获取 MAC 地址 (6 字节)
    virtual std::string get_mac_address() = 0;

    // 发送一个数据包
    virtual bool send(const NetPacket& packet) = 0;

    // 接收一个数据包（非阻塞）
    //   返回 true  = 成功收到一个包，放入 out_packet
    //   返回 false = 当前没有收到包
    virtual bool receive(NetPacket& out_packet) = 0;

    // 处理硬件中断（裸机环境用，虚拟环境留空）
    virtual void handle_interrupt() {}
};