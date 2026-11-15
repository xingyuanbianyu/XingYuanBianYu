#pragma once
#include "INetworkDevice.inc"
#include <iostream>
#include <algorithm>

class LoopbackNetDevice : public INetworkDevice {
    std::string m_mac = "02:00:00:00:00:01"; // 虚拟 MAC 地址
    std::vector<NetPacket> m_rx_queue;       // 接收队列
public:
    bool init() override {
        //std::cout << "[网卡] 回环虚拟网卡初始化成功，MAC: " << m_mac << "\n";
        return true;
    }
    std::string get_mac_address() override { return m_mac; }

    bool send(const NetPacket& packet) override {
        // 回环网卡：把发出去的包直接塞进接收队列
        m_rx_queue.push_back(packet);
        std::cout << "[网卡] 已发送 " << packet.len << " 字节数据包\n";
        return true;
    }

    bool receive(NetPacket& out_packet) override {
        if (m_rx_queue.empty()) return false;
        out_packet = m_rx_queue.front();
        m_rx_queue.erase(m_rx_queue.begin());
        return true;
    }
};