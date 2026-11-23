#pragma once
#include <string>

namespace llm {

struct ProviderError {
    int http_status = 0;   // 0 = 没走到 HTTP 层（连接失败 / 超时 / 被取消）
    std::string code;      // 厂商错误码字符串，如 invalid_api_key；没有就留空
    std::string message;   // 厂商错误体里的 message；或 libcurl 的错误描述
    bool cancelled = false;
};

} // namespace llm
