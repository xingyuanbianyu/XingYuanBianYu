#include "http_server.h"
#include "../../VFS/include/vfs.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <fstream>
#include <thread>
#include <atomic>
#include <functional>

// ==========================================================
// 🚀 全局状态控制
// ==========================================================
static std::atomic<bool> g_server_running{false};
static int g_server_fd = -1;
static std::thread g_server_thread;

// 🚀 全局回调：由 main.cpp 注入的脚本执行器
std::function<std::string(const std::string& script_content, const std::string& vfs_path)> g_http_script_runner = nullptr;

// 辅助函数：根据后缀返回 Content-Type
static std::string get_mime_type(const std::string& path) {
    if (path.find(".html") != std::string::npos) return "text/html";
    if (path.find(".css") != std::string::npos) return "text/css";
    if (path.find(".js") != std::string::npos) return "application/javascript";
    if (path.find(".png") != std::string::npos) return "image/png";
    if (path.find(".jpg") != std::string::npos) return "image/jpeg";
    if (path.find(".json") != std::string::npos) return "application/json";
    return "text/plain";
}

// 处理单个客户端连接（独立线程）
static void handle_client(int client_fd, const std::string& root_path) {
    char buffer[4096] = {0};
    int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }

    std::string request(buffer, bytes_read);
    size_t method_end = request.find(' ');
    if (method_end == std::string::npos) { close(client_fd); return; }
    size_t path_end = request.find(' ', method_end + 1);
    if (path_end == std::string::npos) { close(client_fd); return; }

    std::string req_path = request.substr(method_end + 1, path_end - method_end - 1);
    
    // 🚀 裁剪查询参数 (比如 /api/test?name=123 -> /api/test)
    size_t q_pos = req_path.find('?');
    if (q_pos != std::string::npos) {
        req_path = req_path.substr(0, q_pos);
    }

    // 🚀 防御：过滤路径穿越攻击
    if (req_path.find("..") != std::string::npos) {
        std::string forbidden = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, forbidden.c_str(), forbidden.length(), 0);
        close(client_fd);
        return;
    }

    if (req_path == "/") req_path = "/index.html";

    std::string response;
    bool handled = false;

    // ==========================================================
    // 🚀 动态 API 路由分发：/api/xxx
    // ==========================================================
    if (req_path.rfind("/api/", 0) == 0) {
        handled = true;
        std::string body_result = "{\"error\": \"API runtime not available\"}";
        
        if (g_http_script_runner) {
            // 尝试寻找 .xy 脚本
            std::string api_script_path = root_path + req_path + ".xy";
            auto node = VFS::getInstance().get_node(api_script_path);
            
            if (!node) {
                // 如果没有 .xy，尝试找 .xyn
                api_script_path = root_path + req_path + ".xyn";
                node = VFS::getInstance().get_node(api_script_path);
            }

            if (node && !node->is_dir) {
                // 找到了脚本，读取内容并交给 main.cpp 去执行
                std::string script_content = VFS::getInstance().read_file(api_script_path);
                body_result = g_http_script_runner(script_content, api_script_path);
            } else {
                body_result = "{\"error\": \"API script not found\"}";
            }
        }

        response = "HTTP/1.1 200 OK\r\n"
                   "Content-Type: application/json\r\n"
                   "Content-Length: " + std::to_string(body_result.length()) + "\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Connection: close\r\n\r\n" + body_result;
    }

    // ==========================================================
    // 静态文件路由
    // ==========================================================
    if (!handled) {
        std::string vfs_path = root_path + req_path;
        auto node = VFS::getInstance().get_node(vfs_path);
        
        if (node && !node->is_dir) {
            std::string content = VFS::getInstance().read_file(vfs_path);
            std::string mime = get_mime_type(vfs_path);
            
            response = "HTTP/1.1 200 OK\r\n"
                       "Content-Type: " + mime + "\r\n"
                       "Content-Length: " + std::to_string(content.length()) + "\r\n"
                       "Connection: close\r\n\r\n" + content;
        } else {
            std::string not_found = "<h1>404 Not Found</h1><p>星缘OS 找不到该资源</p>";
            response = "HTTP/1.1 404 Not Found\r\n"
                       "Content-Type: text/html\r\n"
                       "Content-Length: " + std::to_string(not_found.length()) + "\r\n"
                       "Connection: close\r\n\r\n" + not_found;
        }
    }

    send(client_fd, response.c_str(), response.length(), 0);
    close(client_fd);
}

// ==========================================================
// 启动与停止逻辑
// ==========================================================
void start_http_server(int port, const std::string& root_path) {
    if (g_server_running.load()) return;

    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        std::cerr << "[Server] Socket 创建失败\n";
        return;
    }

    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(g_server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "[Server] 端口 " << port << " 绑定失败\n";
        close(g_server_fd);
        g_server_fd = -1;
        return;
    }

    if (listen(g_server_fd, 10) < 0) {
        std::cerr << "[Server] 监听失败\n";
        close(g_server_fd);
        g_server_fd = -1;
        return;
    }

    g_server_running.store(true);

    g_server_thread = std::thread([port, root_path]() {
        while (g_server_running.load()) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(g_server_fd, (struct sockaddr *)&client_addr, &client_len);
            
            if (client_fd >= 0) {
                if (!g_server_running.load()) {
                    close(client_fd);
                    break;
                }
                std::thread(handle_client, client_fd, root_path).detach();
            } else {
                if (!g_server_running.load()) break;
                usleep(10000);
            }
        }
    });

    std::cerr << "\n[Server] HTTP 服务已在端口 " << port << " 启动，根目录: " << root_path << "\n";
}

void stop_http_server() {
    if (!g_server_running.load()) return;
    
    g_server_running.store(false);
    
    if (g_server_fd >= 0) {
        shutdown(g_server_fd, SHUT_RDWR);
        close(g_server_fd);
        g_server_fd = -1;
    }

    if (g_server_thread.joinable()) {
        g_server_thread.join();
    }
    
    std::cerr << "[Server] HTTP 服务已停止\n";
}