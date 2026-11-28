#include "llm/qwen.hpp"

#include <cstdlib>
#include <string>

namespace llm {
namespace {

std::string env_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    if (value != nullptr && value[0] != '\0') return value;
    return fallback;
}

}  // namespace

QwenProvider::QwenProvider()
    : OpenAICompatProvider(
          "qwen",
          env_or("QWEN_BASE_URL",
                 "https://dashscope.aliyuncs.com/compatible-mode/v1"),
          "DASHSCOPE_API_KEY", "qwen-plus") {}

std::string QwenProvider::apiKey() const {
    const char* primary = std::getenv("DASHSCOPE_API_KEY");
    if (primary != nullptr && primary[0] != '\0') return primary;

    const char* fallback = std::getenv("QWEN_API_KEY");
    if (fallback != nullptr && fallback[0] != '\0') return fallback;
    return {};
}

}  // namespace llm
