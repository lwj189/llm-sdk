# llm-sdk

C++17 大模型接入 SDK（W1）。不用任何官方 SDK，只用 libcurl 自己实现 HTTP + SSE 层，
把「用户消息」翻译成「模型逐字返回的流」。

## 当前状态

D4 代码与本地验收完成：`DeepSeekProvider`（`include/llm/deepseek.hpp`）+ OpenAI 兼容 provider（`include/llm/provider.hpp` / `src/providers/openai_compat.cpp`）已实现，`./build/llmchat "问题"` 可逐字流式输出，Ctrl-C 取消；`[DONE]` 早停、HTTP 错误体、非流式 `chat()` 都覆盖；D2 的 `--http-smoke*` 已按 D-01 删除。clean build 0 警告、`ctest` 1/1；本地 mock 的流式 / 非流式 / 401 / 取消全 PASS，真实 401 也已验。**真实 200 尚未验**：手上 key 已失效（401），拿到新 key 后补跑，D4 才算完整。D5 收口待做。

## 文档

- [sdk-notes/如何了解项目现状.md](sdk-notes/如何了解项目现状.md) —— 隔几天回来先看这个：5 分钟搞清做到哪、今天干什么
- [sdk-notes/说明书.md](sdk-notes/说明书.md) —— 接口契约、实现红线、验收标准（先读这个）
- [sdk-notes/W1-实施计划.md](sdk-notes/W1-实施计划.md) —— D1~D5 每天做什么、每步多久
- [NOTES.md](NOTES.md) —— 每日进度与踩坑

## 编译

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build -j

完整用法 / 目录 / 验收见上面的说明书。
