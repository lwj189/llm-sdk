// D2：libcurl 封装。post / post_stream 共用下面唯一一份 perform()。
// 头文件签名由说明书 §6.3 定死；本文件只负责兑现，不改公开接口。

#include "llm/http_client.hpp"

#include <curl/curl.h>

#include <cstddef>
#include <string>

#include "llm/cancel.hpp"

namespace llm {
namespace {

// C 回调拿不到 this：把这次请求要用的 C++ 对象打包成一个 struct，
// 通过 CURLOPT_WRITEDATA / CURLOPT_XFERINFODATA（userdata）传进去，
// 回调里再强转回来。这是「C 库 ↔ C++ 对象」的唯一接缝（说明书 §7.2）。
struct CurlState {
    const HttpClient::ByteSink* sink = nullptr;  // 非空：流式，每块字节交给它
    const CancelToken* cancel = nullptr;         // 非空：可取消
    std::string* body = nullptr;                 // 非空：非流式，整块追到这里
};

// 写回调：返回「本次实际处理的字节数」。返回 0 会让 curl 以
// CURLE_WRITE_ERROR(23) 中止传输 —— 上层说「不要了」就走这条路。
std::size_t on_write(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* st = static_cast<CurlState*>(userdata);
    const std::size_t n = size * nmemb;

    if (st->sink != nullptr) {
        if (!(*st->sink)(ptr, n)) return 0;
        return n;
    }
    if (st->body != nullptr) st->body->append(ptr, n);
    return n;  // body 为空时数据被丢弃，也算处理完
}

// 进度回调：返回非 0 → curl 以 CURLE_ABORTED_BY_CALLBACK(42) 中止。
// 只有把 CURLOPT_NOPROGRESS 设成 0L 它才会被调用；漏了这句，取消就失效。
int on_progress(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* st = static_cast<CurlState*>(userdata);
    return (st->cancel != nullptr && st->cancel->cancelled()) ? 1 : 0;
}

// 全项目唯一一份 curl 配置代码：post / post_stream 都走这里。
// 六项必设（说明书 §7.1）全部落在本函数，review 只看这一段就够。
// 返回值的约定：传输层成功就返回 true —— HTTP 4xx/5xx 也算成功（curl 只认
// 传输层），错误体已经收进 body / 交给 sink，交给上层 parseErrorBody 去解释。
bool perform(const HttpRequest& req, const HttpClient::ByteSink* sink,
             const CancelToken* cancel, std::string* body_out,
             long* http_status, std::string* err) {
    if (http_status != nullptr) *http_status = 0;
    if (err != nullptr) err->clear();
    if (body_out != nullptr) body_out->clear();

    CURL* handle = curl_easy_init();
    if (handle == nullptr) {
        if (err != nullptr) *err = "curl_easy_init failed";
        return false;
    }

    CurlState st;
    st.sink = sink;
    st.cancel = cancel;
    st.body = body_out;

    curl_slist* headers = nullptr;
    for (const std::string& header : req.headers) {
        headers = curl_slist_append(headers, header.c_str());
    }

    // —— 六项必设（说明书 §7.1，计划 §3.2 逐条打勾）——
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);                        // 1 多线程下不用 alarm 做 DNS 超时
    curl_easy_setopt(handle, CURLOPT_HTTP_VERSION,
                     static_cast<long>(CURL_HTTP_VERSION_1_1));           // 2 SSE 只在 HTTP/1.1 上验证过
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &on_write);           // 3 静态函数 + userdata
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &st);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);                     // 4 不设 0L，进度回调永不触发
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, &on_progress);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &st);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, req.connect_timeout_ms);  // 5 毫秒版
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, req.total_timeout_ms);
    curl_easy_setopt(handle, CURLOPT_TCP_KEEPALIVE, 1L);                  // 6 长连接保活

    // —— 请求本身 ——
    curl_easy_setopt(handle, CURLOPT_URL, req.url.c_str());
    if (headers != nullptr) {
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    }
    curl_easy_setopt(handle, CURLOPT_POST, 1L);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, req.body.c_str());
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(req.body.size()));

    // 低速放弃：req 里给的字段，0 = 不启用（说明书 §6.3）。
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, req.low_speed_limit);
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, req.low_speed_time_s);

    const CURLcode code = curl_easy_perform(handle);

    // 状态码无论成败都取一次：401 时 perform 也是成功，错误体在 body 里；
    // 连不上时这里填 0（= 请求根本没走到 HTTP 层）。
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    if (http_status != nullptr) *http_status = status;

    bool ok = false;
    if (code == CURLE_OK) {
        ok = true;
    } else if (err != nullptr) {
        // 错误文本只写 curl 的原文 + CURLcode，不自己编故事（计划 D-08）：
        //   7  = 连不上        28 = 超时
        //   23 = 写回调返回 0  42 = 进度回调返回 1（= 取消）
        // 取消的最终判定在调用侧：cancel.cancelled() 为真且是 23/42 之一。
        // 本层签名里没有 ProviderError（§6.3 定死），所以 cancelled 标志
        // 由 D4 的 provider 依据 cancel.cancelled() 填，不是这里丢掉了。
        *err = std::string(curl_easy_strerror(code)) + " (CURLcode " +
               std::to_string(static_cast<long>(code)) + ")";
    }

    if (headers != nullptr) curl_slist_free_all(headers);
    curl_easy_cleanup(handle);
    return ok;
}

}  // namespace

void HttpClient::globalInit() {
    // 整个进程调一次即可；curl_global_init 不是线程安全的，所以放在 main 开头。
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void HttpClient::globalCleanup() { curl_global_cleanup(); }

bool HttpClient::post(const HttpRequest& req, std::string* body_out,
                      long* http_status, std::string* err) {
    return perform(req, nullptr, nullptr, body_out, http_status, err);
}

bool HttpClient::post_stream(const HttpRequest& req, const ByteSink& on_bytes,
                             const CancelToken& cancel, long* http_status,
                             std::string* err) {
    return perform(req, &on_bytes, &cancel, nullptr, http_status, err);
}

}  // namespace llm
