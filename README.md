# llm-sdk

C++17 大模型接入 SDK（W1）。不用任何官方 SDK，只用 libcurl 自己实现 HTTP + SSE 层，
把「用户消息」翻译成「模型逐字返回的流」。

## 当前状态

W1 已完成（D1–D5）。同一套 `LLMProvider` / `OpenAICompatProvider` 已接三家：

| provider | 默认模型 | base_url | key |
| --- | --- | --- | --- |
| `glm`（默认） | `glm-4-flash` | `https://open.bigmodel.cn/api/paas/v4` | `GLM_API_KEY` / `ZHIPUAI_API_KEY` |
| `qwen` | `qwen-plus` | `https://dashscope.aliyuncs.com/compatible-mode/v1` | `DASHSCOPE_API_KEY` / `QWEN_API_KEY` |
| `deepseek` | `deepseek-chat` | `https://api.deepseek.com` | `DEEPSEEK_API_KEY` |

三家真实 API 已在 2026-11-28 验到 HTTP 200，逐字流式、usage、Ctrl-C 取消都通过；
clean build 0 警告、`ctest` 1/1、SSE 真实夹具 4 用例 ALL PASS。完整原始输出见
[sdk-notes/W1-验收报告.md](sdk-notes/W1-验收报告.md)。

## 用法

    # 默认 GLM；key 只从环境变量读取，不会写盘或打印
    GLM_API_KEY=... ./build/llmchat "用三句话解释 epoll"

    # 千问 / DeepSeek
    DASHSCOPE_API_KEY=... ./build/llmchat --provider qwen "用三句话解释 epoll"
    DEEPSEEK_API_KEY=... ./build/llmchat --provider deepseek "用三句话解释 epoll"

    # 换模型；查看全部参数
    ./build/llmchat --provider qwen --model qwen-plus "你好"
    ./build/llmchat --help

GLM 也接受 `ZHIPUAI_API_KEY`，千问也接受 `QWEN_API_KEY` 作为回退。调试时可设置
`GLM_BASE_URL` / `QWEN_BASE_URL` 覆盖端点（默认值见 provider 头文件）。

## 目录

    include/llm/     ir / cancel / error / http_client / sse / provider / 三家 provider 头
    src/             HTTP 客户端、SSE 解析器、OpenAI 兼容 provider 与三家默认值
    examples/        流式 CLI（逐字输出、usage、SIGINT 取消）
    tests/           SSE 四用例、真实与手写夹具
    sdk-notes/       说明书、W1 实施计划、验收报告、项目现状

## 设计三句话

1. **IR 隔离厂商差异**：`Message` / `ChatRequest` / `StreamChunk` 定义在
   `include/llm/ir.hpp`，上层只依赖 IR，不依赖任何厂商 JSON。
2. **模板方法**：`OpenAICompatProvider::chatStream` 写成 `final`，只留 6 个
   protected 钩子；DeepSeek / GLM / 千问只填默认值，GLM 额外把流式 usage 字段改成
   `chunk_include_usage`。
3. **取消贯穿到底**：CLI 的 SIGINT 写入 `CancelToken`，HTTP 层通过 curl 进度回调
   返回 `CURLE_ABORTED_BY_CALLBACK`，CLI 以 130 退出。

## 文档

- [sdk-notes/W1-验收报告.md](sdk-notes/W1-验收报告.md) —— 三家真实 200、流式、取消与六条验收证据
- [sdk-notes/如何了解项目现状.md](sdk-notes/如何了解项目现状.md) —— 隔几天回来先看这个：5 分钟搞清做到哪
- [sdk-notes/说明书.md](sdk-notes/说明书.md) —— 接口契约、实现红线、验收标准（先读这个）
- [sdk-notes/W1-实施计划.md](sdk-notes/W1-实施计划.md) —— D1~D5 每天做什么、每步多久
- [NOTES.md](NOTES.md) —— 每日进度与踩坑

## 编译

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build -j
