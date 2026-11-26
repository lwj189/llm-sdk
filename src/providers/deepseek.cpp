#include "llm/deepseek.hpp"

namespace llm {

DeepSeekProvider::DeepSeekProvider()
    : OpenAICompatProvider("deepseek", "https://api.deepseek.com",
                           "DEEPSEEK_API_KEY", "deepseek-chat") {}

}  // namespace llm
