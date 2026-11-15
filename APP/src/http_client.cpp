#include "http_client.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <filesystem> // 🚀 新增：用于临时文件操作

// 🚀 引入 Socket 和 OpenSSL 头文件
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>

#include <openssl/ssl.h>
#include <openssl/err.h>

struct ParsedUrl {
    std::string host;
    int port = 80;
    std::string path;
    bool is_https = false;
};

static bool parse_url(const std::string& url, ParsedUrl& out) {
    std::string rest;
    if (url.rfind("http://", 0) == 0) {
        rest = url.substr(7);
        out.port = 80;
        out.is_https = false;
    } else if (url.rfind("https://", 0) == 0) {
        rest = url.substr(8);
        out.port = 443;
        out.is_https = true;
    } else {
        return false;
    }
    
    size_t slash_pos = rest.find('/');
    std::string host_port = (slash_pos == std::string::npos) ? rest : rest.substr(0, slash_pos);
    out.path = (slash_pos == std::string::npos) ? "/" : rest.substr(slash_pos);
    
    size_t colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
        out.host = host_port.substr(0, colon_pos);
        try { out.port = std::stoi(host_port.substr(colon_pos + 1)); } catch (...) { return false; }
    } else {
        out.host = host_port;
    }
    return !out.host.empty();
}

// 辅助函数：不区分大小写地查找字符串
static bool icontains(const std::string& str, const std::string& substr) {
    std::string lower_str = str;
    std::string lower_sub = substr;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);
    std::transform(lower_sub.begin(), lower_sub.end(), lower_sub.begin(), ::tolower);
    return lower_str.find(lower_sub) != std::string::npos;
}

std::string http_download(const std::string& url, const std::string& host_dest_path) {
    ParsedUrl parsed;
    if (!parse_url(url, parsed)) {
        return "非法的 URL 格式";
    }

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    // 1. DNS 解析
    struct addrinfo hints, *addr_res;
    std::memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(parsed.port);
    if (getaddrinfo(parsed.host.c_str(), port_str.c_str(), &hints, &addr_res) != 0) {
        return "DNS 解析失败: " + parsed.host;
    }

    // 2. 建立 TCP 连接
    int sockfd = socket(addr_res->ai_family, addr_res->ai_socktype, addr_res->ai_protocol);
    if (sockfd < 0) {
        freeaddrinfo(addr_res);
        return "创建 Socket 失败";
    }

    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    if (connect(sockfd, addr_res->ai_addr, addr_res->ai_addrlen) < 0) {
        close(sockfd);
        freeaddrinfo(addr_res);
        return "TCP 连接失败，请检查网络";
    }
    freeaddrinfo(addr_res);

    // 3. 如果是 HTTPS，进行 TLS 握手
    SSL_CTX* ctx = nullptr;
    SSL* ssl = nullptr;
    
    if (parsed.is_https) {
        ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) {
            close(sockfd);
            return "创建 SSL Context 失败";
        }
        
        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, sockfd);
        SSL_set_tlsext_host_name(ssl, parsed.host.c_str());

        if (SSL_connect(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            close(sockfd);
            return "TLS/SSL 握手失败";
        }
    }

    // 4. 构造并发送 HTTP 请求
    std::ostringstream req_oss;
    req_oss << "GET " << parsed.path << " HTTP/1.1\r\n"
            << "Host: " << parsed.host << "\r\n"
            << "User-Agent: XingYuanOS/1.0\r\n"
            << "Accept: */*\r\n"
            << "Connection: close\r\n\r\n";
    std::string request = req_oss.str();

    if (parsed.is_https) {
        SSL_write(ssl, request.c_str(), request.length());
    } else {
        send(sockfd, request.c_str(), request.length(), 0);
    }

    // 5. 接收响应数据
    std::string raw_response;
    char buf[4096];
    int bytes_received;
    
    while (true) {
        if (parsed.is_https) {
            bytes_received = SSL_read(ssl, buf, sizeof(buf));
        } else {
            bytes_received = recv(sockfd, buf, sizeof(buf), 0);
        }
        if (bytes_received <= 0) break;
        raw_response.append(buf, bytes_received);
    }

    // 6. 清理网络资源
    if (parsed.is_https) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
    }
    close(sockfd);

    if (raw_response.empty()) {
        return "服务器未返回任何数据";
    }

    // 7. 解析 HTTP 响应头
    size_t header_end = raw_response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return "HTTP 响应格式错误";
    }

    std::string headers = raw_response.substr(0, header_end);
    std::string body_raw = raw_response.substr(header_end + 4);
    std::string body = body_raw;

    // 🚀 处理 301/302 重定向
    if (icontains(headers, "301 Moved Permanently") || 
        icontains(headers, "302 Found") ||
        icontains(headers, "307 Temporary Redirect") ||
        icontains(headers, "308 Permanent Redirect")) {
        
        size_t loc_pos = headers.find("Location: ");
        if (loc_pos == std::string::npos) loc_pos = headers.find("location: ");
        
        if (loc_pos != std::string::npos) {
            size_t loc_start = loc_pos + 10;
            size_t loc_end = headers.find("\r\n", loc_start);
            std::string redirect_url = headers.substr(loc_start, loc_end - loc_start);
            
            if (redirect_url.rfind("http", 0) != 0) {
                if (redirect_url[0] == '/') {
                    redirect_url = (parsed.is_https ? "https://" : "http://") + parsed.host + redirect_url;
                } else {
                    redirect_url = (parsed.is_https ? "https://" : "http://") + parsed.host + "/" + redirect_url;
                }
            }
            return http_download(redirect_url, host_dest_path);
        }
    }

    // 检查状态码
    if (headers.find("200 OK") == std::string::npos && headers.find("HTTP/1.1 200") == std::string::npos && headers.find("HTTP/1.0 200") == std::string::npos) {
        size_t line_end = headers.find("\r\n");
        return "HTTP 错误: " + headers.substr(0, line_end);
    }

    // 🚀 核心修复：处理 Chunked 分块传输编码
    if (icontains(headers, "Transfer-Encoding: chunked")) {
        std::string decoded_body;
        size_t pos = 0;
        while (pos < body_raw.size()) {
            size_t line_end = body_raw.find("\r\n", pos);
            if (line_end == std::string::npos) break;
            
            std::string hex_len = body_raw.substr(pos, line_end - pos);
            
            // 忽略可能存在的扩展参数 (如 "5;foo=bar")
            size_t semi = hex_len.find(';');
            if (semi != std::string::npos) hex_len = hex_len.substr(0, semi);
            
            size_t chunk_size = 0;
            try {
                chunk_size = std::stoul(hex_len, nullptr, 16);
            } catch (...) {
                break; // 解析失败退出
            }
            
            if (chunk_size == 0) break; // 0 代表结束，完美丢弃它！
            
            pos = line_end + 2; // 越过 \r\n
            if (pos + chunk_size > body_raw.size()) break;
            
            // 提取真实数据
            decoded_body.append(body_raw.substr(pos, chunk_size));
            pos += chunk_size + 2; // 越过分块数据及末尾的 \r\n
        }
        body = decoded_body;
    }

    // 8. 写入宿主机文件
    std::ofstream ofs(host_dest_path, std::ios::binary);
    if (!ofs.is_open()) {
        return "无法创建宿主机临时文件: " + host_dest_path;
    }
    ofs.write(body.data(), body.size());
    ofs.close();

    return ""; // 成功
}

// ============================================================
// 🚀 新增：直接拉取 URL 内容为字符串（供 net browse 使用）
// ============================================================
std::string http_get_string(const std::string& url) {
    std::string tmp_file = ".xyby_tmp/tmp_browse.tmp";
    
    // 确保目录存在
    std::error_code ec;
    std::filesystem::create_directories(".xyby_tmp", ec);
    
    // 下载到临时文件
    std::string err = http_download(url, tmp_file);
    if (!err.empty()) {
        return "[net browse] " + err;
    }
    
    // 读取内容
    std::ifstream ifs(tmp_file, std::ios::binary);
    if (!ifs.is_open()) {
        return "[net browse] 无法读取临时文件";
    }
    
    std::stringstream ss;
    ss << ifs.rdbuf();
    ifs.close();
    
    // 删除临时文件
    std::filesystem::remove(tmp_file, ec);
    
    return ss.str();
}