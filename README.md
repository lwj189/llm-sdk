# llm-sdk

C++17 大模型接入 SDK（W1）。不用任何官方 SDK，只用 libcurl 自己实现 HTTP + SSE 层，
把「用户消息」翻译成「模型逐字返回的流」。

## 当前状态

D2：HTTP 层已完成（`HttpClient::post` / `post_stream`，含取消与超时，`curl` 六项必设齐全）。**还没有对话功能**——SSE 解析器（D3）、provider 与真正的 CLI（D4）尚未实现，目前只能用 `./build/llmchat --http-smoke*` 这组临时冒烟命令验证 HTTP 层。

## 文档

- [sdk-notes/如何了解项目现状.md](sdk-notes/如何了解项目现状.md) —— 隔几天回来先看这个：5 分钟搞清做到哪、今天干什么
- [sdk-notes/说明书.md](sdk-notes/说明书.md) —— 接口契约、实现红线、验收标准（先读这个）
- [sdk-notes/W1-实施计划.md](sdk-notes/W1-实施计划.md) —— D1~D5 每天做什么、每步多久
- [NOTES.md](NOTES.md) —— 每日进度与踩坑

## 编译

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build -j

完整用法 / 目录 / 验收见上面的说明书。
