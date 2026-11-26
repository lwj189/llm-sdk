// D4 流式 CLI：把 DeepSeek 的 SSE 增量逐字打到 stdout，Ctrl-C 走 CancelToken。
//
// 用法：
//   ./build/llmchat "用三句话解释 epoll"
//   DEEPSEEK_API_KEY=... ./build/llmchat "写长一点"   # 运行中按 Ctrl-C 取消
//
// key 只从环境变量 DEEPSEEK_API_KEY 读取，不写进代码，也不会打印。

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <string>

#include "llm/cancel.hpp"
#include "llm/deepseek.hpp"
#include "llm/http_client.hpp"

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
        "llmchat —— DeepSeek 流式聊天\n"
        "\n"
        "用法:\n"
        "  ./build/llmchat \"你的问题\"\n"
        "\n"
        "环境变量:\n"
        "  DEEPSEEK_API_KEY=<key>  必填；只从环境变量读取，不会写盘或打印\n"
        "\n"
        "运行中按 Ctrl-C 会取消当前请求，并在 1 秒内退出。\n");
}

std::string join_args(int argc, char** argv) {
    std::string prompt;
    for (int i = 1; i < argc; ++i) {
        if (!prompt.empty()) prompt.push_back(' ');
        prompt += argv[i];
    }
    return prompt;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 0;
    }

    const std::string prompt = join_args(argc, argv);
    if (prompt.empty()) {
        usage();
        return 2;
    }

    llm::HttpClient::globalInit();

    llm::DeepSeekProvider provider;
    std::string why;
    if (!provider.isAvailable(&why)) {
        std::fprintf(stderr, "[错误] %s\n", why.c_str());
        std::fprintf(stderr, "提示：export DEEPSEEK_API_KEY=... 后重试。\n");
        llm::HttpClient::globalCleanup();
        return 1;
    }

    llm::ChatRequest request;
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
    const bool ok = provider.chatStream(
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
