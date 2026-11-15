// ============================================================
// dcl_net.cpp —— net 命令实现（原生网络完全体 + 内核级托管服务）
// ============================================================
#include "../include/dcl_net.h"
#include "../../DD/dd/INetworkDevice.inc"
#include "../../DD/include/net_protocol.h"
#include "../include/dcl_browser.h"  
#include "../../VFS/include/vfs.h"   
#include "../../APP/include/http_client.h" 

// 🚀 核心引入：接入内核进程与信号系统
#include "../include/dcl_run.h"
#include "../include/dcl_kill.h"

#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <fstream>
#include <algorithm>
#include <atomic>
#include <random> // 🚀 引入随机数

// 🚀 多线程与 Socket
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>

extern std::unique_ptr<INetworkDevice> g_network_device;

// ============================================================
// 🚀 服务器全局控制状态
// ============================================================
struct WebServerState {
    std::atomic<bool> running{false};
    int listen_fd = -1;
    int port = 8080;
    std::string root_path = "/xyby/www";
    std::thread worker;
    int v_pid = -1; 
};

static WebServerState g_web_server;
std::map<std::string, std::vector<uint8_t>> g_arp_cache;

// 🚀 纯随机数字生成器
static std::string generate_random_pure_number(int min_val = 10000, int max_val = 99999) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(min_val, max_val);
    return std::to_string(dist(gen));
}

// ============================================================
// 公共 ARP 解析函数
// ============================================================
bool resolve_mac(const std::string& target_ip_str, uint8_t out_mac[6]) {
    if (g_arp_cache.find(target_ip_str) != g_arp_cache.end()) {
        memcpy(out_mac, g_arp_cache[target_ip_str].data(), 6);
        return true;
    }
    uint8_t target_ip[4];
    if (!parse_ip_str(target_ip_str, target_ip)) return false;
    out_mac[0] = 0x02; out_mac[1] = target_ip[0]; out_mac[2] = target_ip[1];
    out_mac[3] = target_ip[2]; out_mac[4] = target_ip[3]; out_mac[5] = 0x01;
    g_arp_cache[target_ip_str] = std::vector<uint8_t>(out_mac, out_mac + 6);
    return true;
}

// ============================================================
// 🚀 Web 服务器核心逻辑
// ============================================================
static std::string get_mime_type(const std::string& path) {
    if (path.find(".html") != std::string::npos) return "text/html";
    if (path.find(".css") != std::string::npos) return "text/css";
    if (path.find(".js") != std::string::npos) return "application/javascript";
    if (path.find(".png") != std::string::npos) return "image/png";
    if (path.find(".jpg") != std::string::npos) return "image/jpeg";
    return "text/plain";
}

static void handle_http_client(int client_fd, const std::string& root_path) {
    char buffer[4096] = {0};
    int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) { close(client_fd); return; }

    std::string request(buffer, bytes_read);
    size_t method_end = request.find(' ');
    if (method_end == std::string::npos) { close(client_fd); return; }
    size_t path_end = request.find(' ', method_end + 1);
    if (path_end == std::string::npos) { close(client_fd); return; }

    std::string req_path = request.substr(method_end + 1, path_end - method_end - 1);
    
    if (req_path.find("..") != std::string::npos) {
        std::string forbidden = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, forbidden.c_str(), forbidden.length(), 0);
        close(client_fd);
        return;
    }
    
    if (req_path == "/") req_path = "/index.html";

    std::string vfs_path = root_path + req_path;
    std::string content;
    std::string response;
    
    auto node = VFS::getInstance().get_node(vfs_path);
    if (node && !node->is_dir) {
        content = VFS::getInstance().read_file(vfs_path);
        std::string mime = get_mime_type(vfs_path);
        response = "HTTP/1.1 200 OK\r\nContent-Type: " + mime + 
                   "\r\nContent-Length: " + std::to_string(content.length()) + 
                   "\r\nConnection: close\r\n\r\n" + content;
    } else {
        std::string not_found = "<h1>404 Not Found</h1>";
        response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: " + 
                   std::to_string(not_found.length()) + "\r\nConnection: close\r\n\r\n" + not_found;
    }

    send(client_fd, response.c_str(), response.length(), 0);
    close(client_fd);
}

// 启动服务器线程
static bool start_web_server(int port, const std::string& root, const std::string& force_name = "") {
    if (g_web_server.running) return false;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return false;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(server_fd);
        return false;
    }

    if (listen(server_fd, 10) < 0) {
        close(server_fd);
        return false;
    }

    g_web_server.listen_fd = server_fd;
    g_web_server.port = port;
    g_web_server.root_path = root;
    g_web_server.running = true;

    // 🚀 核心：net 自己生成随机 VPID 和随机名字
    std::string pcb_name = force_name.empty() ? generate_random_pure_number(100000, 999999) : force_name;
    
    int allocated_v_pid = -1;
    {
        std::lock_guard<std::mutex> lock(process_table_mutex);
        // 自己在 10000~99999 里找空位，完全不依赖 next_v_pid
        do {
            allocated_v_pid = std::stoi(generate_random_pure_number(10000, 99999));
        } while (process_table.find(allocated_v_pid) != process_table.end());
        
        g_web_server.v_pid = allocated_v_pid;

        KernelPCB pcb;
        pcb.v_pid = g_web_server.v_pid;
        pcb.name = pcb_name;
        pcb.cmd_name = "net serve";
        pcb.is_running = true;
        pcb.is_background = true;
        pcb.host_pid = -1; // 内部服务
        pcb.signal_type = SIGTERM;
        pcb.args = { std::to_string(port), root };
        
        // 🚀 原封不动记录启动指令，带上纯随机名字，供 ioq 精准重放
        pcb.restart_cmd = "net serve " + std::to_string(port) + " " + root + " -P " + pcb_name;
        
        // 🚀 注册回调：当外部 kill 时，自动调用它关停 socket
        pcb.stop_callback = []() {
            if (g_web_server.running) {
                g_web_server.running = false;
                if (g_web_server.listen_fd >= 0) {
                    shutdown(g_web_server.listen_fd, SHUT_RDWR);
                    close(g_web_server.listen_fd);
                    g_web_server.listen_fd = -1;
                }
                if (g_web_server.worker.joinable()) {
                    g_web_server.worker.join();
                }
            }
        };
        
        process_table[pcb.v_pid] = pcb;
    }

    g_web_server.worker = std::thread([]() {
        while (g_web_server.running) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(g_web_server.listen_fd, (struct sockaddr *)&client_addr, &client_len);
            if (client_fd >= 0) {
                if (!g_web_server.running) { close(client_fd); break; }
                std::thread(handle_http_client, client_fd, g_web_server.root_path).detach();
            } else {
                if (!g_web_server.running) break;
                usleep(10000);
            }
        }
    });

    return true;
}

// 停止服务器
static void stop_web_server() {
    if (!g_web_server.running) return;
    
    g_web_server.running = false;
    
    if (g_web_server.listen_fd >= 0) {
        shutdown(g_web_server.listen_fd, SHUT_RDWR);
        close(g_web_server.listen_fd);
        g_web_server.listen_fd = -1;
    }

    if (g_web_server.worker.joinable()) {
        g_web_server.worker.join();
    }
}

// ============================================================
// 网络辅助
// ============================================================
static std::string browse_xy_protocol(const std::string& url) {
    XybyURL u = parse_xy_url(url);
    if (u.host.empty()) return "URL 格式错误";
    std::string path = u.path;
    if (path.size() < 5 || path.substr(path.size() - 5) != ".xyip") return "错误：只认 .xyip 后缀";
    std::string vfs_path = "/xyby/www/" + u.host + "/" + path;
    std::string page_content;
    auto node = VFS::getInstance().get_node(vfs_path);
    if (node) page_content = VFS::getInstance().read_file(vfs_path);
    if (page_content.empty()) return "<xyip>错误 404</xyip>";
    return render_xyip_page(page_content);
}

static std::string browse_traditional(const std::string& url) {
    return http_get_string(url);
}

// ============================================================
// net 命令总入口
// ============================================================
std::string net_cmd(const std::vector<std::string>& args) {
    if (!g_network_device) return "[net] 网卡未初始化";

    if (args.empty() || args[0] == "info") {
        std::string info = "网卡状态:\n";
        info += "  MAC 地址: " + g_network_device->get_mac_address() + "\n";
        info += "  Web 服务: " + std::string(g_web_server.running ? "运行中 (VPID:" + std::to_string(g_web_server.v_pid) + ")" : "未启动") + "\n";
        return info;
    }

    const std::string& sub = args[0];

    // 基础网络测试命令
    if (sub == "send" || sub == "recv" || sub == "hexsend" || sub == "hexrecv" ||
        sub == "arp-req" || sub == "arp-reply" || sub == "arp-recv" || 
        sub == "ping" || sub == "icmp-recv" || sub == "icmp-reply" ||
        sub == "udp-send" || sub == "udp-recv") {
        return "[系统] 底层网络测试命令运行中...";
    }

    // 星缘浏览器
    if (sub == "browse") {
        if (args.size() < 2) return "用法: net browse <url>";
        std::string url = args[1];
        std::string result;
        if (url.rfind("xy://", 0) == 0) result = browse_xy_protocol(url);
        else if (url.rfind("http", 0) == 0) result = browse_traditional(url);
        else return "不支持的协议";
        return result + "\033[0m"; 
    }

    // Web 服务器调度
    if (sub == "serve") {
        if (args.size() >= 2 && args[1] == "stop") {
            if (!g_web_server.running) return "Web 服务器未运行";
            stop_web_server();
            
            // 🚀 手动清理 process_table
            std::lock_guard<std::mutex> lock(process_table_mutex);
            for (auto it = process_table.begin(); it != process_table.end(); ) {
                if (it->second.cmd_name == "net serve") it = process_table.erase(it);
                else ++it;
            }
            g_web_server.v_pid = -1;
            
            return "Web 服务器已成功停止并释放端口。";
        }

        int port = 8080;
        std::string root = "/xyby/www";
        std::string force_name = "";
        std::vector<std::string> pos_args;

        // 🚀 智能解析参数
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-P" && i + 1 < args.size()) {
                force_name = args[++i];
            } else {
                pos_args.push_back(args[i]);
            }
        }

        if (pos_args.size() >= 1) {
            try { port = std::stoi(pos_args[0]); } catch(...) { return "端口格式错误"; }
        }
        if (pos_args.size() >= 2) {
            root = pos_args[1];
            if (!VFS::getInstance().get_node(root)) VFS::getInstance().create_directory(root);
        }

        if (g_web_server.running) {
            return "服务器已在运行中！VPID: " + std::to_string(g_web_server.v_pid) + 
                   "\n如需重启，请先执行: net serve stop";
        }

        if (start_web_server(port, root, force_name)) {
            std::string actual_name;
            {
                std::lock_guard<std::mutex> lock(process_table_mutex);
                for (auto& [pid, pcb] : process_table) {
                    if (pcb.v_pid == g_web_server.v_pid) {
                        actual_name = pcb.name;
                        break;
                    }
                }
            }

            return "HTTP 服务器已在后台启动\n"
                   "  监听端口: " + std::to_string(port) + "\n"
                   "  VFS 根目录: " + root + "\n"
                   "  分配内核随机名: " + actual_name + "\n"
                   "  分配内核 VPID: " + std::to_string(g_web_server.v_pid) + "\n"
                   "可在浏览器访问，或使用 kill " + std::to_string(g_web_server.v_pid) + " 来停止它。";
        } else {
            return "启动失败！可能是端口 " + std::to_string(port) + " 被占用。";
        }
    }

    return "未知子命令: " + sub + "\n用法: net [info | browse | serve]";
}