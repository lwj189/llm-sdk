#pragma once
#include <string>
#include <vector>

namespace llm {

// 角色定义
enum class Role { System, User, Assistant, Tool };

//请求侧
    // 工具调用定义
    struct ToolCall {
        std::string id;// 工具调用 ID，唯一标识一次调用
        std::string name;// 工具名称，调用时传入的 name
        std::string arguments_json;   // 厂商返回的原始 JSON 字符串，不在这一层解析
    };

    // 消息定义
    struct Message {
        Role role = Role::User;
        std::string content;
        std::vector<ToolCall> tool_calls;  // 仅 Assistant 可能非空（W1 不用）
        std::string tool_call_id;          // 仅 Tool 可能非空（W1 不用）
    };

    // 聊天请求定义
    struct ChatRequest {
        std::string model;// 模型名称
        std::vector<Message> messages;// 聊天消息列表
        double temperature = 0.7;// 采样温度，越大越随机，越小越确定
        int max_tokens = 1024;// 最大生成 token 数量，超过则截断
        bool stream = true;// 是否流式返回，流式返回时会多次回调 StreamChunk
    };

// 响应侧
    // 聊天响应定义
    struct Usage {
        int prompt_tokens = 0;// 消耗的 prompt token 数量
        int completion_tokens = 0;// 消耗的 completion token 数量
        int total_tokens = 0;// 消耗的总 token 数量
    };

    // 聊天流式响应定义
    struct StreamChunk {
        std::string text;           // 正文增量，可能为空（比如只带 usage 的最后一帧）
        std::string reasoning;      // 思维链增量，非推理模型恒为空
        std::string finish_reason;  // "stop" / "length" / ""（流未结束）
        bool done = false;          // 本流已结束，收到后不应再有回调
        bool has_usage = false;     // 是否包含 usage 信息
        Usage usage;                // 消耗的 token 数量，has_usage 为 true 时才有效
    };

    // 聊天响应定义
    struct ChatResult {
        Message message;            // role 固定 Assistant
        Usage usage;                // 消耗的 token 数量
    };

} // namespace llm
