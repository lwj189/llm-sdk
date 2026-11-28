# llm-sdk

C++17 大模型接入 SDK（W1）。不用任何官方 SDK，只用 libcurl 自己实现 HTTP + SSE 层，
把「用户消息」翻译成「模型逐字返回的流」。

## 当前状态

D4 代码与本地验收完成，并把 OpenAI 兼容层扩展到 DeepSeek / GLM / 千问三家：
include/llm/provider.hpp + deepseek.hpp / glm.hpp / qwen.hpp，
src/providers/{openai_compat,deepseek,glm,qwen}.cpp。./build/llmchat 默认走 GLM，
可用 --provider qwen|deepseek 切换，--model 覆盖模型；逐字流式、Ctrl-C 取消、
[DONE] 早停、HTTP 错误体、非流式 chat() 都已覆盖。clean build 0 警告、ctest 1/1；
本地 mock 下 GLM / 千问的流式与 usage 全 PASS。**真实 200 尚未验**：DeepSeek 与 GLM
的 key 当前都返回 401（GLM 为「身份验证失败」），千问没有 key；拿到有效 key 后补跑，
D4 才算完整。D5 收口待做。

## 用法

    # 默认 GLM；key 只从环境变量读取，不会写盘或打印
    GLM_API_KEY=... ./build/llmchat "用三句话解释 epoll"

    # 千问 / DeepSeek
    DASHSCOPE_API_KEY=... ./build/llmchat --provider qwen "你好"
    DEEPSEEK_API_KEY=... ./build/llmchat --provider deepseek "你好"

    # 换模型；查看全部参数
    ./build/llmchat --provider qwen --model qwen-plus "你好"
    ./build/llmchat --help

GLM 也接受 ZHIPUAI_API_KEY，千问也接受 QWEN_API_KEY 作为回退。调试时可设置
GLM_BASE_URL / QWEN_BASE_URL 覆盖端点（默认值见 provider 头文件）。

## 文档

- [sdk-notes/如何了解项目现状.md](sdk-notes/如何了解项目现状.md) —— 隔几天回来先看这个：5 分钟搞清做到哪、今天干什么
- [sdk-notes/说明书.md](sdk-notes/说明书.md) —— 接口契约、实现红线、验收标准（先读这个）
- [sdk-notes/W1-实施计划.md](sdk-notes/W1-实施计划.md) —— D1~D5 每天做什么、每步多久
- [NOTES.md](NOTES.md) —— 每日进度与踩坑

## 编译

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build -j

完整用法 / 目录 / 验收见上面的说明书。
