#pragma once

#include <functional>
#include <string>

#include "llm/cancel.hpp"
#include "llm/error.hpp"
#include "llm/ir.hpp"
#include "llm/sse.hpp"

namespace llm {

// 厂商无关的聊天接口。上层只依赖这个类，不关心 HTTP / SSE 细节。
class LLMProvider {
public:
    virtual ~LLMProvider() = default;

    // 厂商名，例如 "deepseek"
    virtual std::string name() const = 0;

    // 当前环境是否具备调用条件；why 非空时写入原因（例如缺 key）
    virtual bool isAvailable(std::string* why) const = 0;

    // 非流式：一次返回完整消息
    virtual bool chat(const ChatRequest& req, ChatResult* out,
                      ProviderError* err) = 0;

    // 流式回调：返回 false 表示调用方不要了，provider 应尽快中断
    using ChunkSink = std::function<bool(const StreamChunk&)>;

    // 流式：每解析出一个 chunk 就回调 on_chunk；cancel 置位后尽快返回
    virtual bool chatStream(const ChatRequest& req, const ChunkSink& on_chunk,
                            const CancelToken& cancel,
                            ProviderError* err) = 0;
};

// OpenAI Chat Completions 兼容实现的模板。DeepSeek 只覆盖默认值，
// 其余厂商以后也可以复用这个类。
class OpenAICompatProvider : public LLMProvider {
public:
    OpenAICompatProvider(std::string name, std::string base_url,
                         std::string api_key_env, std::string default_model);

    std::string name() const final;
    bool isAvailable(std::string* why) const final;
    bool chat(const ChatRequest& req, ChatResult* out,
              ProviderError* err) final;
    bool chatStream(const ChatRequest& req, const ChunkSink& on_chunk,
                    const CancelToken& cancel, ProviderError* err) final;

protected:
    // 子类可覆盖下面几个钩子，模板方法本身保持 final。
    virtual std::string endpoint() const;       // 完整 URL
    virtual std::string apiKey() const;         // 从环境变量读；没有就空串
    virtual std::string buildBody(const ChatRequest& req) const;
    virtual bool parseChunk(const SSEEvent& ev, StreamChunk* out,
                            ProviderError* err) const;
    virtual bool isDoneMark(const SSEEvent& ev) const;
    virtual bool parseErrorBody(const std::string& body,
                                ProviderError* err) const;

private:
    std::string name_;
    std::string base_url_;
    std::string api_key_env_;
    std::string default_model_;
};

} // namespace llm
