#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <cstdio>
#include <vector>
#include <map>

// ============================================================
// 【新增】ARP 缓存表 (IP -> MAC)
// 注意：需要在任意一个 .cpp 文件（如 dcl_net.cpp 或 main.cpp）中
// 定义一次：std::map<std::string, std::vector<uint8_t>> g_arp_cache;
// ============================================================
extern std::map<std::string, std::vector<uint8_t>> g_arp_cache;

// ============================================================
// 以太网帧头（14 字节）
// ============================================================
struct EthernetHeader {
    uint8_t  dst_mac[6];      // 目标 MAC
    uint8_t  src_mac[6];      // 源 MAC
    uint16_t ether_type;      // 协议类型：0x0806 = ARP，0x0800 = IPv4
} __attribute__((packed));    // 【核心】禁止内存对齐，保证严格 14 字节

// ============================================================
// ARP 报文（28 字节）
// ============================================================
struct ArpPacket {
    uint16_t hw_type;         // 硬件类型：1 = 以太网
    uint16_t proto_type;      // 协议类型：0x0800 = IPv4
    uint8_t  hw_len;          // MAC 长度：6
    uint8_t  proto_len;       // IP 长度：4
    uint16_t opcode;          // 操作码：1 = 请求，2 = 应答
    uint8_t  sender_mac[6];   // 发送方 MAC
    uint8_t  sender_ip[4];    // 发送方 IP
    uint8_t  target_mac[6];   // 目标 MAC（请求时全 0）
    uint8_t  target_ip[4];    // 目标 IP
} __attribute__((packed));

// ============================================================
// 完整的以太网 ARP 帧（42 字节）
// ============================================================
struct FullArpFrame {
    EthernetHeader eth;
    ArpPacket      arp;
} __attribute__((packed));

// ============================================================
// IPv4 头部（20 字节，无选项）
// ============================================================
struct IpHeader {
    uint8_t  version_ihl;     // 版本(4) + 首部长度(4) -> 0x45
    uint8_t  tos;             // 服务类型 -> 0
    uint16_t total_len;       // 总长度（IP头 + 数据）
    uint16_t id;              // 标识
    uint16_t flags_offset;    // 标志 + 分片偏移
    uint8_t  ttl;             // 生存时间 -> 64
    uint8_t  protocol;        // 协议：1=ICMP, 6=TCP, 17=UDP
    uint16_t checksum;        // 首部校验和
    uint8_t  src_ip[4];       // 源 IP
    uint8_t  dst_ip[4];       // 目标 IP
} __attribute__((packed));

// ============================================================
// ICMP 头部（8 字节）
// ============================================================
struct IcmpHeader {
    uint8_t  type;            // 8=请求，0=应答
    uint8_t  code;            // 0
    uint16_t checksum;        // 校验和
    uint16_t id;              // 标识
    uint16_t seq;             // 序列号
} __attribute__((packed));

// ============================================================
// 完整的 IP + ICMP 帧（以太网头 + IP 头 + ICMP 头）
// ============================================================
struct FullIcmpFrame {
    EthernetHeader eth;
    IpHeader       ip;
    IcmpHeader     icmp;
    // 后面可以跟数据
} __attribute__((packed));

// ============================================================
// 工具函数：IP 字符串 → 4 字节
// ============================================================
inline bool parse_ip_str(const std::string& ip_str, uint8_t out[4]) {
    int a, b, c, d;
    if (sscanf(ip_str.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return false;
    if (a < 0 || a > 255 || b < 0 || b > 255 ||
        c < 0 || c > 255 || d < 0 || d > 255) return false;
    out[0] = (uint8_t)a;
    out[1] = (uint8_t)b;
    out[2] = (uint8_t)c;
    out[3] = (uint8_t)d;
    return true;
}

// ============================================================
// 工具函数：MAC 字符串 → 6 字节
// ============================================================
inline bool parse_mac_str(const std::string& mac_str, uint8_t out[6]) {
    unsigned int v[6];
    if (sscanf(mac_str.c_str(), "%x:%x:%x:%x:%x:%x",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
    for (int i = 0; i < 6; ++i) out[i] = (uint8_t)v[i];
    return true;
}

// ============================================================
// 工具函数：格式化输出 MAC / IP
// ============================================================
inline std::string format_mac(const uint8_t mac[6]) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

inline std::string format_ip(const uint8_t ip[4]) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    return buf;
}

// ============================================================
// UDP 头部（8 字节）
// ============================================================
struct UdpHeader {
    uint16_t src_port;    // 源端口
    uint16_t dst_port;    // 目标端口
    uint16_t length;      // 总长度（UDP头 + 数据）
    uint16_t checksum;    // 校验和（IPv4 下可选，但建议计算）
} __attribute__((packed));

// UDP 完整帧
struct FullUdpFrame {
    EthernetHeader eth;
    IpHeader       ip;
    UdpHeader      udp;
    // 后面可以跟数据
} __attribute__((packed));

// 星缘OS 专属协议（XYBY-Owned Protocol）
struct XybyPacketHeader {
    uint8_t  state;        // 状态：0=启动, 1=传输中, 2=关闭
    uint8_t  data_type;    // 数据类型：0=字节, 1=二进制, 2=混合进制
    uint16_t table_size;   // 混合进制表的长度（若是其他类型，填 0）
    uint32_t payload_len;  // 实际数据长度
    // 紧接着后面跟着 [表数据] + [实际数据]
};

// 留痕记录结构（存在内存或文件中，随包携带或单独记录）
struct XybyAuditLog {
    std::string sender;    // 谁发送的
    std::string receiver;  // 谁接收的
    std::string viewer;    // 谁查看的（未知填 "Unknown"）
};
struct XybyURL {
    std::string host;     // 比如 "example.xingyuan"
    std::string path;     // 比如 "/index.xyby"
    uint16_t port = 8080; // 默认端口
};

// 解析 "xy://example.xingyuan/index.xyby"
XybyURL parse_xy_url(const std::string& url) {
    XybyURL u;
    if (url.rfind("xy://", 0) != 0) return u;
    std::string rest = url.substr(5);
    size_t slash = rest.find('/');
    if (slash == std::string::npos) {
        u.host = rest;
        u.path = "/index.xyip";
    } else {
        u.host = rest.substr(0, slash);
        u.path = rest.substr(slash);
    }
    return u;
}

// ============================================================
// 【新增】网络校验和计算 (RFC 1071)
// 用于 IP 头部和 ICMP 报文
// ============================================================
inline uint16_t calculate_checksum(const void* data, size_t len) {
    const uint16_t* ptr = (const uint16_t*)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len > 0) {
        sum += *(const uint8_t*)ptr;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return ~sum;
}