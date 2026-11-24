// D2 临时驱动：验证 HttpClient 的非流式 / 流式两条路 + 三条错误路径。
// D4 会用真正的流式 CLI 替换掉这整个文件（计划 D-01：不新增 target）。
//
// 用法与环境变量见 usage()；这些 --http-smoke* 分支 D4 会删掉。

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "llm/cancel.hpp"
#include "llm/http_client.hpp"

// 三个类型都在命名空间 llm 里，这个文件里写全名太长，用 using 引进来。
using llm::CancelToken;
using llm::HttpClient;
using llm::HttpRequest;

namespace {

constexpr const char* kDeepSeekUrl = "https://api.deepseek.com/chat/completions";

std::string env_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    if (value != nullptr && value[0] != '\0') return std::string(value);
    return std::string(fallback);
}

long now_ms() {
    using namespace std::chrono;
    return static_cast<long>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// 只打印前 limit 个字符：body 可能是几百 KB 的 JSON，验收时看头就够。
void print_body_head(const std::string& body, std::size_t limit = 200) {
    const std::size_t n = body.size() < limit ? body.size() : limit;
    std::printf("  body.size=%zu\n", body.size());
    std::printf("  body.head=");
    if (n > 0) std::fwrite(body.data(), 1, n, stdout);
    std::printf("%s\n", body.size() > n ? " ..." : "");
}

HttpRequest make_request(const std::string& url, bool stream) {
    HttpRequest req;
    req.url = url;
    req.headers.push_back("Content-Type: application/json");
    req.body = stream
                   ? R"({"model":"deepseek-chat","stream":true,"messages":[{"role":"user","content":"hi"}]})"
                   : R"({"model":"deepseek-chat","stream":false,"messages":[{"role":"user","content":"ping"}]})";
    return req;
}

void add_key_header(HttpRequest* req) {
    const std::string key = env_or("DEEPSEEK_API_KEY", "");
    if (key.empty()) {
        std::fprintf(stderr,
                     "[warn] DEEPSEEK_API_KEY 未设置：本次请求不带 Authorization"
                     "（真实 200 用例需要它；也可以设 LLM_SMOKE_URL 指向本地服务）\n");
        return;
    }
    req->headers.push_back("Authorization: Bearer " + key);
}

// 200：post() 收齐整块响应体。
int smoke_post_200() {
    const std::string url = env_or("LLM_SMOKE_URL", kDeepSeekUrl);
    HttpRequest req = make_request(url, false);
    add_key_header(&req);

    HttpClient client;
    std::string body;
    long status = 0;
    std::string err;
    const long t0 = now_ms();
    const bool ok = client.post(req, &body, &status, &err);
    const long ms = now_ms() - t0;

    std::printf("[smoke-200] url=%s\n", url.c_str());
    std::printf("[smoke-200] status=%ld ok=%d elapsed_ms=%ld err=\"%s\"\n", status,
                ok ? 1 : 0, ms, err.c_str());
    print_body_head(body);

    const bool pass = ok && status == 200;
    std::printf("[smoke-200] %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

// 401：错误 key。重点是 perform 仍然成功，厂商错误原文在 body 里（说明书 §7.5）。
int smoke_post_401() {
    const std::string url = env_or("LLM_SMOKE_URL", kDeepSeekUrl);
    HttpRequest req = make_request(url, false);
    req.headers.push_back("Authorization: Bearer invalid");  // 故意用不像 key 的串（计划 D-07）

    HttpClient client;
    std::string body;
    long status = 0;
    std::string err;
    const bool ok = client.post(req, &body, &status, &err);

    std::printf("[smoke-401] url=%s\n", url.c_str());
    std::printf("[smoke-401] status=%ld ok=%d err=\"%s\"\n", status, ok ? 1 : 0,
                err.c_str());
    std::printf("[smoke-401] 关键：401 时 ok 必须为 1（传输层成功），错误体在 body 里\n");
    print_body_head(body);

    const bool pass = ok && status == 401;
    std::printf("[smoke-401] %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

// 连不通：127.0.0.1:1。期望 http_status=0 + curl 原文。
int smoke_fail() {
    HttpRequest req = make_request("http://127.0.0.1:1/", false);
    req.connect_timeout_ms = 1000;
    req.total_timeout_ms = 3000;

    HttpClient client;
    std::string body;
    long status = 0;
    std::string err;
    const long t0 = now_ms();
    const bool ok = client.post(req, &body, &status, &err);
    const long ms = now_ms() - t0;

    std::printf("[smoke-fail] url=http://127.0.0.1:1/\n");
    std::printf("[smoke-fail] status=%ld ok=%d elapsed_ms=%ld err=\"%s\"\n", status,
                ok ? 1 : 0, ms, err.c_str());
    std::printf("[smoke-fail] 关键：连不上时 http_status=0（没走到 HTTP 层）\n");

    const bool pass = !ok && status == 0 && !err.empty();
    std::printf("[smoke-fail] %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

// 连接超时：connect_timeout_ms=1，必须在 1 秒内返回，不许挂死。
int smoke_timeout() {
    const std::string url = env_or("LLM_SMOKE_URL", kDeepSeekUrl);
    HttpRequest req = make_request(url, false);
    req.connect_timeout_ms = 1;  // 1 毫秒：必然超时
    req.total_timeout_ms = 5000;

    HttpClient client;
    std::string body;
    long status = 0;
    std::string err;
    const long t0 = now_ms();
    const bool ok = client.post(req, &body, &status, &err);
    const long ms = now_ms() - t0;

    std::printf("[smoke-timeout] url=%s\n", url.c_str());
    std::printf("[smoke-timeout] status=%ld ok=%d elapsed_ms=%ld err=\"%s\"\n", status,
                ok ? 1 : 0, ms, err.c_str());
    std::printf("[smoke-timeout] 关键：1 毫秒的连接超时必须 1 秒内返回\n");

    const bool pass = !ok && status == 0 && ms < 1500;
    std::printf("[smoke-timeout] %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

// 流式：post_stream() 每收到一块就回调一次，这里只统计字节数 + 留前 200 字节。
int smoke_stream() {
    const std::string url = env_or("LLM_SMOKE_URL", kDeepSeekUrl);
    HttpRequest req = make_request(url, true);
    add_key_header(&req);

    HttpClient client;
    CancelToken cancel;  // 本用例不取消，只是签名要求传一个
    std::size_t bytes = 0;
    std::size_t blocks = 0;
    std::string head;
    long status = 0;
    std::string err;
    const long t0 = now_ms();
    const bool ok = client.post_stream(
        req,
        [&](const char* p, std::size_t n) {
            ++blocks;
            bytes += n;
            if (head.size() < 200) {
                const std::size_t take = std::min(n, std::size_t{200} - head.size());
                head.append(p, take);
            }
            return true;
        },
        cancel, &status, &err);
    const long ms = now_ms() - t0;

    std::printf("[smoke-stream] url=%s\n", url.c_str());
    std::printf("[smoke-stream] status=%ld ok=%d blocks=%zu bytes=%zu elapsed_ms=%ld err=\"%s\"\n",
                status, ok ? 1 : 0, blocks, bytes, ms, err.c_str());
    std::printf("[smoke-stream] head(200)=");
    if (!head.empty()) std::fwrite(head.data(), 1, head.size(), stdout);
    std::printf("\n");

    const bool pass = ok && status == 200 && bytes > 0;
    std::printf("[smoke-stream] %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

// 取消：收到第一块后置取消位，但本次仍返回 true —— 真正的中断由进度回调
// （on_progress 返回 1 → CURLE_ABORTED_BY_CALLBACK）完成。这条用例专门验证
// CURLOPT_NOPROGRESS=0L 真的生效（计划 §3.2 第 4 条）。
int smoke_cancel() {
    const std::string url = env_or("LLM_SMOKE_URL", kDeepSeekUrl);
    HttpRequest req = make_request(url, true);
    add_key_header(&req);

    HttpClient client;
    CancelToken cancel;
    std::size_t bytes = 0;
    long status = 0;
    std::string err;
    const long t0 = now_ms();
    const bool ok = client.post_stream(
        req,
        [&](const char*, std::size_t n) {
            bytes += n;
            if (!cancel.cancelled()) cancel.cancel();  // 第一块到了就喊停
            return true;
        },
        cancel, &status, &err);
    const long ms = now_ms() - t0;

    const bool cancelled = cancel.cancelled() && !ok;
    std::printf("[smoke-cancel] url=%s\n", url.c_str());
    std::printf("[smoke-cancel] status=%ld ok=%d cancelled=%d bytes=%zu elapsed_ms=%ld err=\"%s\"\n",
                status, ok ? 1 : 0, cancelled ? 1 : 0, bytes, ms, err.c_str());
    std::printf("[smoke-cancel] 关键：取消生效 = token 已置位 + 传输出错（CURLcode 42 或 23）\n");

    const bool pass = cancelled;
    std::printf("[smoke-cancel] %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

void usage() {
    std::printf(
        "llmchat（D2 临时驱动）—— 真正的流式 CLI 在 D4 实现\n"
        "\n"
        "用法:\n"
        "  ./build/llmchat --http-smoke           # post(): 200（需要 DEEPSEEK_API_KEY）\n"
        "  ./build/llmchat --http-smoke-401       # post(): 错误 key -> 401，错误体在 body 里\n"
        "  ./build/llmchat --http-smoke-fail      # post(): 连不通 127.0.0.1:1 -> status=0\n"
        "  ./build/llmchat --http-smoke-timeout   # connect_timeout_ms=1 -> 1 秒内返回\n"
        "  ./build/llmchat --http-smoke-stream    # post_stream(): 逐块回调（需要 key）\n"
        "  ./build/llmchat --http-smoke-cancel    # post_stream(): 收到第一块后取消\n"
        "\n"
        "环境变量:\n"
        "  DEEPSEEK_API_KEY=<key>  请求用的 key（只从环境变量读，不会打印）\n"
        "  LLM_SMOKE_URL=<url>     覆盖目标地址（用本地服务验 200 / 取消时用）\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 0;
    }

    HttpClient::globalInit();
    const std::string cmd = argv[1];
    int rc = 0;
    if (cmd == "--http-smoke") {
        rc = smoke_post_200();
    } else if (cmd == "--http-smoke-401") {
        rc = smoke_post_401();
    } else if (cmd == "--http-smoke-fail") {
        rc = smoke_fail();
    } else if (cmd == "--http-smoke-timeout") {
        rc = smoke_timeout();
    } else if (cmd == "--http-smoke-stream") {
        rc = smoke_stream();
    } else if (cmd == "--http-smoke-cancel") {
        rc = smoke_cancel();
    } else {
        std::fprintf(stderr, "未知参数: %s\n\n", cmd.c_str());
        usage();
        rc = 2;
    }
    HttpClient::globalCleanup();
    return rc;
}
