#pragma once

#include "llm/provider.hpp"

namespace llm {

// 智谱 GLM 的默认值固定在构造函数里：
//   base_url     = https://open.bigmodel.cn/api/paas/v4
//   key 环境变量  = GLM_API_KEY（回退 ZHIPUAI_API_KEY）
//   model        = glm-4-flash
class GlmProvider : public OpenAICompatProvider {
public:
    GlmProvider();

protected:
    std::string apiKey() const override;
    std::string usageOptionKey() const override;
};

}  // namespace llm
