#include "llm/provider.hpp"

#include <cstdlib>
#include <utility>

#include <nlohmann/json.hpp>

#include "llm/http_client.hpp"

namespace llm {
namespace {

using nlohmann::json;

std::string role_name(Role role) {
    switch (role) {
        case Role::System:
            return "system";
        case Role::User:
            return "user";
        case Role::Assistant:
            return "assistant";
        case Role::Tool:
            return "tool";
    }
    return "user";
}

std::string truncate(const std::string& text, std::size_t limit = 200) {
    if (text.size() <= limit) return text;
    return text.substr(0, limit) + "...";
}

void set_error(ProviderError* err, std::string message, std::string code = {}) {
    if (err == nullptr) return;
    err->message = std::move(message);
    err->code = std::move(code);
}

bool parse_usage(const json& value, Usage* usage) {
    if (usage == nullptr || !value.is_object()) return false;

    bool any = false;
    auto take = [&](const char* key, int* dst) {
        const auto it = value.find(key);
        if (it == value.end() || !it->is_number_integer()) return;
        *dst = it->get<int>();
        any = true;
    };
    take("prompt_tokens", &usage->prompt_tokens);
    take("completion_tokens", &usage->completion_tokens);
    take("total_tokens", &usage->total_tokens);
    return any;
}

json parse_json(const std::string& text) {
    return json::parse(text, nullptr, false);
}

}  // namespace

OpenAICompatProvider::OpenAICompatProvider(std::string name, std::string base_url,
                                           std::string api_key_env,
                                           std::string default_model)
    : name_(std::move(name)),
      base_url_(std::move(base_url)),
      api_key_env_(std::move(api_key_env)),
      default_model_(std::move(default_model)) {}

std::string OpenAICompatProvider::name() const { return name_; }

std::string OpenAICompatProvider::endpoint() const {
    std::string base = base_url_;
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base + "/chat/completions";
}

std::string OpenAICompatProvider::apiKey() const {
    const char* value = std::getenv(api_key_env_.c_str());
    if (value == nullptr) return {};
    return std::string(value);
}

std::string OpenAICompatProvider::usageOptionKey() const {
    return "include_usage";
}

bool OpenAICompatProvider::isAvailable(std::string* why) const {
    if (!apiKey().empty()) return true;
    if (why != nullptr) {
        *why = "环境变量 " + api_key_env_ + " 未设置";
    }
    return false;
}

std::string OpenAICompatProvider::buildBody(const ChatRequest& req) const {
    json messages = json::array();
    for (const auto& message : req.messages) {
        json item;
        item["role"] = role_name(message.role);
        item["content"] = message.content;

        if (!message.tool_calls.empty()) {
            json calls = json::array();
            for (const auto& call : message.tool_calls) {
                calls.push_back(json{
                    {"id", call.id},
                    {"type", "function"},
                    {"function", json{{"name", call.name},
                                      {"arguments", call.arguments_json}}},
                });
            }
            item["tool_calls"] = std::move(calls);
        }
        if (!message.tool_call_id.empty()) {
            item["tool_call_id"] = message.tool_call_id;
        }
        messages.push_back(std::move(item));
    }

    json body;
    body["model"] = req.model.empty() ? default_model_ : req.model;
    body["messages"] = std::move(messages);
    body["temperature"] = req.temperature;
    body["max_tokens"] = req.max_tokens;
    body["stream"] = req.stream;
    if (req.stream) {
        // 只有流式请求才带这个字段，避免影响非流式响应。
        body["stream_options"] = json{{usageOptionKey(), true}};
    }
    return body.dump();
}

bool OpenAICompatProvider::parseErrorBody(const std::string& body,
                                          ProviderError* err) const {
    if (err == nullptr) return false;

    err->code.clear();
    err->message.clear();
    if (body.empty()) {
        err->message = "HTTP error with empty body";
        return true;
    }

    const json value = parse_json(body);
    if (!value.is_discarded() && value.contains("error")) {
        const auto& error = value["error"];
        if (error.is_object()) {
            if (error.contains("message") && error["message"].is_string()) {
                err->message = error["message"].get<std::string>();
            }
            if (error.contains("code") && error["code"].is_string()) {
                err->code = error["code"].get<std::string>();
            } else if (error.contains("type") && error["type"].is_string()) {
                err->code = error["type"].get<std::string>();
            }
        } else if (error.is_string()) {
            err->message = error.get<std::string>();
        }
    }

    if (err->message.empty()) err->message = body;
    return true;
}

bool OpenAICompatProvider::parseChunk(const SSEEvent& ev, StreamChunk* out,
                                      ProviderError* err) const {
    if (out == nullptr) {
        set_error(err, "parseChunk: out 为空");
        return false;
    }
    *out = StreamChunk{};

    if (ev.data.empty()) return true;

    const json value = parse_json(ev.data);
    if (value.is_discarded()) {
        set_error(err, "SSE data 不是合法 JSON: " + truncate(ev.data));
        return false;
    }

    if (value.contains("error")) {
        const auto& error = value["error"];
        std::string message = "stream error";
        std::string code;
        if (error.is_object()) {
            if (error.contains("message") && error["message"].is_string()) {
                message = error["message"].get<std::string>();
            }
            if (error.contains("code") && error["code"].is_string()) {
                code = error["code"].get<std::string>();
            } else if (error.contains("type") && error["type"].is_string()) {
                code = error["type"].get<std::string>();
            }
        } else if (error.is_string()) {
            message = error.get<std::string>();
        }
        set_error(err, std::move(message), std::move(code));
        return false;
    }

    if (value.contains("choices")) {
        const auto& choices = value["choices"];
        if (!choices.is_array()) {
            set_error(err, "响应里的 choices 不是数组");
            return false;
        }
        if (!choices.empty()) {
            const auto& choice = choices[0];
            if (!choice.is_object()) {
                set_error(err, "响应里的 choices[0] 不是对象");
                return false;
            }

            if (choice.contains("delta") && choice["delta"].is_object()) {
                const auto& delta = choice["delta"];
                if (delta.contains("content") && delta["content"].is_string()) {
                    out->text = delta["content"].get<std::string>();
                }
                if (delta.contains("reasoning_content") &&
                    delta["reasoning_content"].is_string()) {
                    out->reasoning =
                        delta["reasoning_content"].get<std::string>();
                } else if (delta.contains("reasoning") &&
                           delta["reasoning"].is_string()) {
                    out->reasoning = delta["reasoning"].get<std::string>();
                }
            }

            if (choice.contains("finish_reason") &&
                choice["finish_reason"].is_string()) {
                out->finish_reason =
                    choice["finish_reason"].get<std::string>();
            }
        }
    }

    if (value.contains("usage")) {
        out->has_usage = parse_usage(value["usage"], &out->usage);
    }
    return true;
}

bool OpenAICompatProvider::isDoneMark(const SSEEvent& ev) const {
    return ev.data == "[DONE]";
}

bool OpenAICompatProvider::chat(const ChatRequest& req, ChatResult* out,
                                ProviderError* err) {
    if (err != nullptr) *err = ProviderError{};
    if (out == nullptr) {
        set_error(err, "chat: out 为空");
        return false;
    }
    *out = ChatResult{};

    std::string why;
    if (!isAvailable(&why)) {
        set_error(err, std::move(why));
        return false;
    }

    ChatRequest actual = req;
    actual.stream = false;

    HttpRequest http;
    http.url = endpoint();
    http.headers.push_back("Content-Type: application/json");
    http.headers.push_back("Authorization: Bearer " + apiKey());
    http.body = buildBody(actual);

    HttpClient client;
    std::string body;
    long status = 0;
    std::string transport_error;
    const bool ok =
        client.post(http, &body, &status, &transport_error);

    if (err != nullptr) err->http_status = static_cast<int>(status);

    if (status >= 300) {
        parseErrorBody(body, err);
        return false;
    }
    if (!ok) {
        set_error(err, transport_error.empty() ? "HTTP 传输失败"
                                               : transport_error);
        return false;
    }

    const json value = parse_json(body);
    if (value.is_discarded()) {
        set_error(err, "响应不是合法 JSON: " + truncate(body));
        return false;
    }
    if (value.contains("error")) {
        parseErrorBody(body, err);
        return false;
    }
    if (!value.contains("choices") || !value["choices"].is_array() ||
        value["choices"].empty()) {
        set_error(err, "响应里没有 choices");
        return false;
    }

    const auto& choice = value["choices"][0];
    if (!choice.is_object() || !choice.contains("message") ||
        !choice["message"].is_object()) {
        set_error(err, "响应里没有 choices[0].message");
        return false;
    }

    const auto& message = choice["message"];
    out->message.role = Role::Assistant;
    if (message.contains("content") && message["content"].is_string()) {
        out->message.content = message["content"].get<std::string>();
    }

    if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
        for (const auto& item : message["tool_calls"]) {
            if (!item.is_object()) continue;
            ToolCall call;
            if (item.contains("id") && item["id"].is_string()) {
                call.id = item["id"].get<std::string>();
            }
            if (item.contains("function") && item["function"].is_object()) {
                const auto& function = item["function"];
                if (function.contains("name") && function["name"].is_string()) {
                    call.name = function["name"].get<std::string>();
                }
                if (function.contains("arguments")) {
                    if (function["arguments"].is_string()) {
                        call.arguments_json =
                            function["arguments"].get<std::string>();
                    } else {
                        call.arguments_json = function["arguments"].dump();
                    }
                }
            }
            out->message.tool_calls.push_back(std::move(call));
        }
    }

    if (value.contains("usage")) {
        parse_usage(value["usage"], &out->usage);
    }
    return true;
}

bool OpenAICompatProvider::chatStream(const ChatRequest& req,
                                      const ChunkSink& on_chunk,
                                      const CancelToken& cancel,
                                      ProviderError* err) {
    if (err != nullptr) *err = ProviderError{};

    std::string why;
    if (!isAvailable(&why)) {
        set_error(err, std::move(why));
        return false;
    }

    ChatRequest actual = req;
    actual.stream = true;

    HttpRequest http;
    http.url = endpoint();
    http.headers.push_back("Content-Type: application/json");
    http.headers.push_back("Authorization: Bearer " + apiKey());
    http.body = buildBody(actual);

    HttpClient client;
    SSEParser parser;
    std::string raw;
    bool done_seen = false;
    bool done_callback_sent = false;
    bool finish_seen = false;
    bool parse_failed = false;
    bool stopped_by_sink = false;
    long status = 0;
    std::string transport_error;

    auto emit_done = [&]() {
        done_seen = true;
        if (done_callback_sent) return;
        done_callback_sent = true;
        if (!on_chunk) return;
        StreamChunk done;
        done.done = true;
        on_chunk(done);
    };

    auto handle_event = [&](const SSEEvent& ev) -> bool {
        if (isDoneMark(ev)) {
            emit_done();
            return false;
        }

        StreamChunk chunk;
        if (!parseChunk(ev, &chunk, err)) {
            parse_failed = true;
            return false;
        }
        if (!chunk.finish_reason.empty()) finish_seen = true;

        const bool meaningful = !chunk.text.empty() || !chunk.reasoning.empty() ||
                                !chunk.finish_reason.empty() || chunk.has_usage;
        if (meaningful && on_chunk && !on_chunk(chunk)) {
            stopped_by_sink = true;
            return false;
        }
        return true;
    };

    auto sink = [&](const char* data, std::size_t size) -> bool {
        raw.append(data, size);
        for (const auto& event : parser.feed(data, size)) {
            if (!handle_event(event)) return false;
        }
        return true;
    };

    const bool ok =
        client.post_stream(http, sink, cancel, &status, &transport_error);

    if (err != nullptr) err->http_status = static_cast<int>(status);

    if (!done_seen && !parse_failed && !stopped_by_sink) {
        for (const auto& event : parser.finish()) {
            if (!handle_event(event)) break;
        }
    }

    if (done_seen) {
        return true;
    }

    if (status >= 300) {
        parseErrorBody(raw, err);
        return false;
    }

    if (cancel.cancelled()) {
        if (err != nullptr) {
            err->cancelled = true;
            if (err->message.empty()) err->message = "请求已取消";
        }
        return false;
    }

    if (stopped_by_sink) {
        if (err != nullptr) {
            err->cancelled = true;
            if (err->message.empty()) err->message = "on_chunk 返回 false，已停止";
        }
        return false;
    }

    if (parse_failed) {
        return false;
    }

    if (!ok) {
        set_error(err, transport_error.empty() ? "HTTP 传输失败"
                                               : transport_error);
        return false;
    }

    if (finish_seen) {
        // 有些兼容实现不发 [DONE]，但给了 finish_reason；这里补一个结束回调。
        emit_done();
        return true;
    }

    set_error(err, "流在 [DONE] / finish_reason 之前结束");
    return false;
}

}  // namespace llm
