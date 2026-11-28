#include "llm/glm.hpp"

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

GlmProvider::GlmProvider()
    : OpenAICompatProvider("glm",
                           env_or("GLM_BASE_URL",
                                  "https://open.bigmodel.cn/api/paas/v4"),
                           "GLM_API_KEY", "glm-4-flash") {}

std::string GlmProvider::apiKey() const {
    const char* primary = std::getenv("GLM_API_KEY");
    if (primary != nullptr && primary[0] != '\0') return primary;

    // 智谱官方文档用的是 ZHIPUAI_API_KEY，这里保留回退，减少配置踩坑。
    const char* fallback = std::getenv("ZHIPUAI_API_KEY");
    if (fallback != nullptr && fallback[0] != '\0') return fallback;
    return {};
}

std::string GlmProvider::usageOptionKey() const {
    // GLM 的流式用量开关叫 chunk_include_usage，不是 OpenAI 的 include_usage。
    return "chunk_include_usage";
}

}  // namespace llm
