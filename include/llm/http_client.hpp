#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace llm {

class CancelToken;   // 这里只用到引用，前向声明就够，不用把 cancel.hpp 整个拉进来

// 一次 HTTP 请求的全部原料。与厂商无关，provider 层只负责把它填好。
struct HttpRequest {
    std::string url;
    std::vector<std::string> headers;   // 每条形如 "Content-Type: application/json"
    std::string body;
    long connect_timeout_ms = 5000;     // 连接超时（毫秒，不是秒）
    long total_timeout_ms   = 120000;   // 整个请求的总超时
    long low_speed_limit    = 1;        // 每秒低于这么多字节就放弃；0 = 不启用
    long low_speed_time_s   = 30;
};

class HttpClient {
public:
    // “每收到一块字节就调用一次”。返回 false 表示我不要了，主动中断。
    using ByteSink = std::function<bool(const char*, std::size_t)>;

    static void globalInit();     // main 开头调一次（内部 curl_global_init）
    static void globalCleanup();  // 可选，进程结束时调

    // 非流式：等响应全部收完，整块放进 body_out。
    bool post(const HttpRequest& req, std::string* body_out,
              long* http_status, std::string* err);

    // 流式：每收到一块字节就回调 on_bytes，直到它返回 false，或被 cancel 中断。
    bool post_stream(const HttpRequest& req, const ByteSink& on_bytes,
                     const CancelToken& cancel,
                     long* http_status, std::string* err);
};

} // namespace llm
