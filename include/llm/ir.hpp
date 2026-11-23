#pragma once
#include <string>
#include <vector>

namespace llm {

enum class Role { System, User, Assistant, Tool };

struct ToolCall {
    std::string id;
    std::string name;
    std::string arguments_json;   // 厂商返回的原始 JSON 字符串，不在这一层解析
};

struct Message {
    Role role = Role::User;
    std::string content;
    std::vector<ToolCall> tool_calls;  // 仅 Assistant 可能非空（W1 不用）
    std::string tool_call_id;          // 仅 Tool 可能非空（W1 不用）
};

struct ChatRequest {
    std::string model;
    std::vector<Message> messages;
    double temperature = 0.7;
    int max_tokens = 1024;
    bool stream = true;
};

struct Usage {
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
};

struct StreamChunk {
    std::string text;           // 正文增量，可能为空（比如只带 usage 的最后一帧）
    std::string reasoning;      // 思维链增量，非推理模型恒为空
    std::string finish_reason;  // "stop" / "length" / ""（流未结束）
    bool done = false;          // 本流已结束，收到后不应再有回调
    bool has_usage = false;
    Usage usage;
};

struct ChatResult {
    Message message;            // role 固定 Assistant
    Usage usage;
};

} // namespace llm
