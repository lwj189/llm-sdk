#pragma once

#include "llm/provider.hpp"

namespace llm {

// DeepSeek 的默认值固定在构造函数里：
//   base_url   = https://api.deepseek.com
//   key 环境变量 = DEEPSEEK_API_KEY
//   model      = deepseek-chat
class DeepSeekProvider : public OpenAICompatProvider {
public:
    DeepSeekProvider();
};

} // namespace llm
