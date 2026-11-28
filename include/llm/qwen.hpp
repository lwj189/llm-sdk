#pragma once

#include "llm/provider.hpp"

namespace llm {

// 阿里云百炼 / 通义千问的 OpenAI 兼容入口：
//   base_url     = https://dashscope.aliyuncs.com/compatible-mode/v1
//   key 环境变量  = DASHSCOPE_API_KEY（回退 QWEN_API_KEY）
//   model        = qwen-plus
class QwenProvider : public OpenAICompatProvider {
public:
    QwenProvider();

protected:
    std::string apiKey() const override;
};

}  // namespace llm
