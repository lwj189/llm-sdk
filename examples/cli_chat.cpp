// D4 流式 CLI：把 OpenAI 兼容 provider 的 SSE 增量逐字打到 stdout，
// Ctrl-C 走 CancelToken。默认接 GLM，可用 --provider 切换。
//
// 用法：
//   GLM_API_KEY=... ./build/llmchat "用三句话解释 epoll"
//   ./build/llmchat --provider qwen --model qwen-plus "你好"
//   ./build/llmchat --provider deepseek "写长一点"
//
// key 只从环境变量读取，不写进代码，也不会打印。

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>

#include "llm/cancel.hpp"
#include "llm/deepseek.hpp"
#include "llm/glm.hpp"
#include "llm/http_client.hpp"
#include "llm/provider.hpp"
#include "llm/qwen.hpp"

namespace {

llm::CancelToken* g_cancel = nullptr;

extern "C" void handle_sigint(int) {
    if (g_cancel != nullptr) g_cancel->cancel();
}

long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
        .count();
}

void usage() {
    std::printf(
        "llmchat —— OpenAI 兼容的流式聊天\n"
        "\n"
        "用法:\n"
        "  ./build/llmchat [--provider glm|qwen|deepseek] [--model 模型名] \"你的问题\"\n"
        "\n"
        "provider 默认 glm；key 只从环境变量读取，不会写盘或打印:\n"
        "  glm       GLM_API_KEY（回退 ZHIPUAI_API_KEY）\n"
        "  qwen      DASHSCOPE_API_KEY（回退 QWEN_API_KEY）\n"
        "  deepseek  DEEPSEEK_API_KEY\n"
        "\n"
        "示例:\n"
        "  GLM_API_KEY=... ./build/llmchat --provider glm \"用三句话解释 epoll\"\n"
        "  DASHSCOPE_API_KEY=... ./build/llmchat --provider qwen \"你好\"\n"
        "\n"
        "运行中按 Ctrl-C 会取消当前请求，并在 1 秒内退出。\n");
}

std::unique_ptr<llm::LLMProvider> make_provider(const std::string& name) {
    if (name == "glm") return std::make_unique<llm::GlmProvider>();
    if (name == "qwen") return std::make_unique<llm::QwenProvider>();
    if (name == "deepseek") return std::make_unique<llm::DeepSeekProvider>();
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    std::string provider_name = "glm";
    std::string model;
    std::string prompt;
    bool help = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            help = true;
            break;
        }
        if (arg == "--provider") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "[错误] --provider 缺少参数\n");
                return 2;
            }
            provider_name = argv[++i];
            continue;
        }
        if (arg == "--model") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "[错误] --model 缺少参数\n");
                return 2;
            }
            model = argv[++i];
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "[错误] 未知参数: %s\n", arg.c_str());
            return 2;
        }
        if (!prompt.empty()) prompt.push_back(' ');
        prompt += arg;
    }

    if (help) {
        usage();
        return 0;
    }
    if (prompt.empty()) {
        usage();
        return 2;
    }

    auto provider = make_provider(provider_name);
    if (!provider) {
        std::fprintf(stderr, "[错误] 未知 provider: %s（可选 glm / qwen / deepseek）\n",
                     provider_name.c_str());
        return 2;
    }

    llm::HttpClient::globalInit();

    std::string why;
    if (!provider->isAvailable(&why)) {
        std::fprintf(stderr, "[错误] %s\n", why.c_str());
        std::fprintf(stderr, "提示：按 --help 里的环境变量设置 key 后重试。\n");
        llm::HttpClient::globalCleanup();
        return 1;
    }

    std::fprintf(stderr, "[provider] %s\n", provider->name().c_str());

    llm::ChatRequest request;
    request.model = model;
    request.messages.push_back(llm::Message{llm::Role::User, prompt, {}, {}});

    llm::CancelToken cancel;
    g_cancel = &cancel;
    std::signal(SIGINT, handle_sigint);

    bool printed_text = false;
    bool printed_newline = false;
    bool reasoning_started = false;
    bool has_usage = false;
    llm::Usage usage;

    const long long started = now_ms();
    llm::ProviderError error;
    const bool ok = provider->chatStream(
        request,
        [&](const llm::StreamChunk& chunk) {
            if (!chunk.reasoning.empty()) {
                if (!reasoning_started) {
                    std::fprintf(stderr, "\n[reasoning] ");
                    reasoning_started = true;
                }
                std::fwrite(chunk.reasoning.data(), 1, chunk.reasoning.size(),
                            stderr);
                std::fflush(stderr);
            }

            if (!chunk.text.empty()) {
                const std::size_t written =
                    std::fwrite(chunk.text.data(), 1, chunk.text.size(), stdout);
                std::fflush(stdout);
                if (written != chunk.text.size()) return false;
                printed_text = true;
            }

            if (chunk.has_usage) {
                has_usage = true;
                usage = chunk.usage;
            }

            if (chunk.done && !printed_newline) {
                std::fputc('\n', stdout);
                std::fflush(stdout);
                printed_newline = true;
            }
            return true;
        },
        cancel, &error);

    g_cancel = nullptr;
    const long long elapsed = now_ms() - started;

    if (reasoning_started) std::fputc('\n', stderr);
    if (!printed_newline && printed_text) {
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }

    if (!ok) {
        std::fprintf(stderr, "\n[错误] 请求失败");
        if (!error.cancelled && error.http_status > 0) {
            std::fprintf(stderr, "（HTTP %d）", error.http_status);
        }
        if (!error.code.empty()) {
            std::fprintf(stderr, " code=%s", error.code.c_str());
        }
        std::fprintf(stderr, ": %s\n", error.message.c_str());
        std::fprintf(stderr, "[耗时] %lld ms\n", elapsed);
        llm::HttpClient::globalCleanup();
        return error.cancelled ? 130 : 1;
    }

    if (has_usage) {
        std::fprintf(stderr,
                     "[usage] prompt=%d completion=%d total=%d\n",
                     usage.prompt_tokens, usage.completion_tokens,
                     usage.total_tokens);
    }
    std::fprintf(stderr, "[done] %lld ms\n", elapsed);

    llm::HttpClient::globalCleanup();
    return 0;
}
