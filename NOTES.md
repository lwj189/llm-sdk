# W1 开发笔记

> 项目：llm-sdk —— 不用官方 SDK，只用 libcurl 手写 HTTP + SSE 的 C++17 大模型接入库。
> 状态：W1 已完成（D1–D5，2026-11-28）；DeepSeek / GLM / 千问三家真实 HTTP 200 已验收。
> 验收提交：`cdc804c`（docs 收口）；代码提交 `f48546f`。当前 HEAD 以 `git log -1` 为准。
> 配套文档：`README.md`（用法 / 目录 / 设计）、`sdk-notes/W1-验收报告.md`（完整证据）、
> `sdk-notes/W1-实施计划.md`（每天做什么）。

## 目录

- [五天概览](#五天概览)
- [D1：骨架与 IR](#d1骨架与-ir2026-11-22)
- [D2：HTTP 层](#d2http-层2026-11-24)
- [D3：SSE 解析器](#d3sse-解析器2026-11-25)
- [D4：Provider + 流式 CLI](#d4provider--流式-cli2026-11-26-起2026-11-28-收口)
- [D5：收口](#d5收口2026-11-28)
- [关键观察与坑](#关键观察与坑)

## 五天概览

| Day | 主题 | 结果 |
| --- | --- | --- |
| D1 | 目录骨架 + IR / 取消 / 错误类型 | clean build 0 警告；`ctest` 1/1；仓库已推送 |
| D2 | libcurl HTTP 客户端（`post` / `post_stream`） | 200 / stream / cancel / 401 / fail / timeout 六条全 PASS |
| D3 | SSE 解析器 + 逐字节重放测试 | 真实夹具 32494 字节；四用例 ALL PASS；Release 同样通过 |
| D4 | Provider + 流式 CLI + GLM / 千问扩展 | 三家真实 HTTP 200；逐字输出、usage、Ctrl-C 全部通过 |
| D5 | 文档收口 + 六条验收 | 0 警告、`ctest` 1/1、三家 200、无 key；验收时 16 个提交 |

---

## D1：骨架与 IR（2026-11-22）

- **做了什么**：目录骨架 + `ir.hpp` / `cancel.hpp` / `error.hpp` + 附录原样的 CMakeLists + 6 个占位翻译单元；在真实目录 `~/桌面/sdk` 重新 configure/build，零警告，`ctest` 1/1（占位 main，不含断言）。说明书与计划放在 `sdk-notes/`，随仓库提交。
- **卡在哪**：沙箱已放开，骨架已经落盘；`nlohmann-json3-dev`（3.11.3）与 `libsqlite3-dev`（3.46.1）已装好。git 仓库已建并推到 GitHub（`bdee496` 首提交、`1fbcd64` 文档整改、`8603175` D1 收尾）。
- **明天第一件事**：D2 —— `HttpClient::post` 打通，跑通 200 / 401 / 连接失败三条路径。

### D1 验收（2026-11-23 复跑）

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   → exit=0
cmake --build build -j                          → exit=0，warning: 0，error: 0
ctest --test-dir build --output-on-failure      → 100% tests passed, 1/1
./build/llmchat                                 → "D1 骨架，功能在 D4 实现"，exit=0
git grep -nE 'sk-[A-Za-z0-9_-]{16,}'            → 0 命中
git log --oneline                               → bdee496、1fbcd64、8603175（origin/main 已同步）
```

## D2：HTTP 层（2026-11-24）

- **做了什么**：`src/http_client.cpp` 写满——`CurlState` + `on_write` / `on_progress` 两个 C 回调、全项目唯一一份 `perform()`（六项必设都在这一段）、`post()` 把响应体收进 `body_out`、`post_stream()` 逐块回调 + 取消；`examples/cli_chat.cpp` 加 6 个 `--http-smoke*` 临时驱动（D4 已按 D-01 删除）。clean build 0 警告，`ctest` 1/1。
- **卡在哪**：机器上仍然没有 `DEEPSEEK_API_KEY`（Q4 / D-09），所以「200」用本地 python 假服务 + 假 key 验（`LLM_SMOKE_URL` 覆盖地址）；401 / fail / timeout 打真实 `api.deepseek.com`。真实 key 的 200 到 D3 拿到 key 后补跑。
- **明天第一件事**：D3 —— 有 key 先录 `tests/fixtures/deepseek_stream.sse`；没 key 就按 D-09 先写解析器 + 手写夹具，把 4 个用例跑通。

### D2 验收（2026-11-24，clean build）

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   → exit=0
cmake --build build -j                          → exit=0，warning: 0，error: 0
ctest --test-dir build --output-on-failure      → 100% tests passed, 1/1

A. 200（本地假服务，LLM_SMOKE_URL=http://127.0.0.1:18080/chat/completions，
   DEEPSEEK_API_KEY=dummy-local-key）
   [smoke-200] status=200 ok=1 elapsed_ms=1 err=""；body.size=224；PASS

B. stream（同一个本地服务，chunked SSE 5 块、每块隔 0.4s）
   [smoke-stream] status=200 ok=1 blocks=5 bytes=228 elapsed_ms=2002；PASS
   说明：确实是逐块回调，不是攒完再打印。

C. cancel（收到第一块就把取消位置 1）
   [smoke-cancel] status=200 ok=0 cancelled=1 bytes=52 elapsed_ms=1
   err="Operation was aborted by an application callback (CURLcode 42)"；PASS

D. 401（真实 api.deepseek.com，Authorization: Bearer invalid）
   [smoke-401] status=401 ok=1 err=""；body.size=204
   body.head={"error":{"message":"Authentication Fails, Your api key: ****alid is invalid ...
   注意：传输层成功，所以 ok=1；HTTP 状态码要单独取。

E. fail（127.0.0.1:1）
   [smoke-fail] status=0 ok=0 err="Could not connect to server (CURLcode 7)"；PASS

F. timeout（connect_timeout_ms=1）
   [smoke-timeout] status=0 ok=0 elapsed_ms=5 err="Timeout was reached (CURLcode 28)"；PASS

G. key 扫描：grep -rnE "sk-[A-Za-z0-9_-]{16,}" → 0 命中
```

## D3：SSE 解析器（2026-11-25）

- **做了什么**：`include/llm/sse.hpp` + `src/sse.cpp` 实现 SSE 解析器——`feed` 行扫描、半行/半事件挂起、跨块 CRLF、`:` 注释、多行 `data`、`finish` 幂等；`tests/test_sse.cpp` 四个用例（整读 / 逐字节重放 / 混合边界 / 残行 finish）；新增手写夹具 `tests/fixtures/handwritten_stream.sse`；按说明书 §8.1 用真实 key 录 `tests/fixtures/deepseek_stream.sse`（32494 字节 / 236 行 / 118 条 `data` / 117 条 JSON chunk + `[DONE]`）。clean build 0 警告，`ctest` 1/1，真实夹具四例全 PASS；Release（`NDEBUG`）构建同样 0 警告、`ctest` 1/1、四例 PASS。
- **卡在哪**：原卡点是机器上没有 `DEEPSEEK_API_KEY`，真实夹具无法录；现已录到并重跑通过，D3 没有遗留问题。
- **明天第一件事**：D4 —— 先建 `include/llm/deepseek.hpp`，再填 `src/providers/openai_compat.cpp` 的 `buildBody` / `chat` / `chatStream`，最后接流式 CLI（计划 §5）。

> 真实夹具观察：响应里的 `model` 是 `deepseek-flash`（请求写的是 `deepseek-chat`）；不要拿响应 model 做一致性校验。

### D3 验收（2026-11-25，clean build）

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   → exit=0
cmake --build build -j                          → exit=0，warning: 0，error: 0
ctest --test-dir build --output-on-failure      → 100% tests passed, 1/1

./build/llm_tests                               → exit=0
=== D3 SSE parser tests ===
fixture: /home/aaa/桌面/sdk/tests/fixtures/deepseek_stream.sse  (recorded)
[case 1] whole fixture
[case 2] byte-by-byte replay
[case 3] mixed boundaries
[case 4] finish residual
ALL PASS

Release（-DCMAKE_BUILD_TYPE=Release，NDEBUG）同样 0 警告、ctest 1/1、四例 PASS。
git grep -nE 'sk-[A-Za-z0-9_-]{16,}'            → 0 命中
```

## D4：Provider + 流式 CLI（2026-11-26 起，2026-11-28 收口）

- **做了什么**：新增 `include/llm/provider.hpp`（`LLMProvider` + 模板方法式 `OpenAICompatProvider`）与 `include/llm/deepseek.hpp`；填满 `src/providers/openai_compat.cpp` 的 `endpoint` / `apiKey` / `isAvailable` / `buildBody` / `chat` / `chatStream` / `parseChunk` / `parseErrorBody`；`src/providers/deepseek.cpp` 填 DeepSeek 默认值；`examples/cli_chat.cpp` 改成真正的流式 CLI（逐字打印、reasoning 打到 stderr、usage、SIGINT → `CancelToken`、退出码 130），按 D-01 删除全部 `--http-smoke*`。11-28 又按用户要求扩展 GLM / 千问：新增 `glm.hpp` / `qwen.hpp` 与对应 `.cpp`，`usageOptionKey()` 钩子默认 `include_usage`、GLM 覆盖为 `chunk_include_usage`；CLI 支持 `--provider glm|qwen|deepseek` 与 `--model`，默认 `glm`，保留 DeepSeek。
- **卡在哪**：11-26 DeepSeek 旧 key 真实请求 401，真实 200 未验；本地 mock 的流式 / 非流式 / 401 / 取消四条全 PASS。11-28 GLM key 也返回 401（标准端点与 coding 端点、多个模型都试过），千问没有 key；之后拿到有效 key，三家真实 200 才补齐。
- **明天第一件事**：D5 收口；补 D4 真实 200 与 Ctrl-C，按说明书 §9 跑六条验收 + 四问自测，最后 `docs:` commit。

### D4-A：本地 mock 与 401（2026-11-26）

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   → exit=0
cmake --build build -j                          → exit=0，warning: 0，error: 0
ctest --test-dir build --output-on-failure      → 100% tests passed, 1/1

./build/llmchat                                 → usage，exit=0
./build/llmchat "hi"（无 key）                  → exit=1
  [错误] 环境变量 DEEPSEEK_API_KEY 未设置
DEEPSEEK_API_KEY=invalid ./build/llmchat "hi"   → exit=1
  [错误] 请求失败（HTTP 401） code=invalid_request_error: Authentication Fails, Your api key: **** is invalid
  [耗时] 469 ms

本地 mock（/tmp/mock_deepseek.py，127.0.0.1:18080；临时文件已清理）：
  nonstream ok=1 content=你好，世界 usage=3/4/7
  stream ok=1 chunks=117 done=1 usage=1 len=459 text_head=1. epoll 是 Linux ...
  401 ok=0 len=0
  401-chat ok=0
  sink-false ok=0 chunks=1 cancelled=1
  cancel ok=0 chunks=2 cancelled=1 elapsed_ms=200
  ALL PASS

临时 CLI 指向本地 mock：
  /slow 上 0.5s 时 Ctrl-C → rc=130，约 500ms 回到 shell，stdout "012"，
  stderr "[错误] 请求失败: 请求已取消"
  /fast 上 rc=0，stderr "[usage] prompt=10 completion=115 total=125"、"[done] 5455 ms"

git grep -nE 'sk-[A-Za-z0-9_-]{16,}'            → 0 命中
```

### D4-B：GLM / 千问请求体（2026-11-28，本地 mock）

```text
cmake --build build -j                          → exit=0，warning: 0，error: 0
ctest --test-dir build --output-on-failure      → 100% tests passed, 1/1

本地 mock（127.0.0.1:18081）：
  glm_rc=0  stdout="你好，世界"
    stderr=[provider] glm / [usage] prompt=3 completion=2 total=5 / [done] 3 ms
  qwen_rc=0 stdout="你好，世界"
    stderr=[provider] qwen / [usage] prompt=3 completion=2 total=5 / [done] 1 ms
  请求体：
    glm  model=glm-4-flash stream_options={"chunk_include_usage": true}
    qwen model=qwen-plus   stream_options={"include_usage": true}

真实 GLM（当时 key 只放临时进程环境，未写盘、未打印）：
  [provider] glm
  [错误] 请求失败（HTTP 401） code=1000: 身份验证失败。
  [耗时] 334 ms

git grep -nE 'sk-[A-Za-z0-9_-]{16,}'            → 0 命中
```

> 上面的 401 是历史记录；D4-C 用有效 key 收口后，D4 已 ✅。

### D4-C：三家真实 200（2026-11-28 收口）

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   → exit=0
cmake --build build -j                          → exit=0，warning: 0，error: 0
ctest --test-dir build --output-on-failure      → 100% tests passed, 1/1
./build/llm_tests                               → fixture recorded，[case 1]~[case 4] ALL PASS

./build/llmchat --provider glm "用一句话解释 epoll"
  [provider] glm
  epoll 是一种高效的 I/O 多路复用机制……
  [usage] prompt=9 completion=32 total=41
  [done] 1946 ms

./build/llmchat --provider qwen "用一句话解释 epoll"
  [provider] qwen
  epoll 是 Linux 内核提供的一种高效 I/O 事件通知机制……
  [usage] prompt=12 completion=82 total=94
  [done] 2420 ms

./build/llmchat --provider deepseek "用一句话解释 epoll"
  [provider] deepseek
  epoll 是 Linux 内核提供的一种 I/O 多路复用机制……
  [usage] prompt=9 completion=52 total=61
  [done] 851 ms

显式 HTTP 状态（非流式，只回复 OK）：
  glm:      http=200 model=glm-4-flash    content='OK'
  qwen:     http=200 model=qwen-plus      content='OK'
  deepseek: http=200 model=deepseek-flash content='OK'

三句话流式计时（单次实测，不作为 p50 / p95）：
  glm:      首 chunk 594.2 ms / 78 chunks / 总 5685.6 ms / usage 10/94/104
  qwen:     首 chunk 567.5 ms / 38 chunks / 总 4024.5 ms / usage 13/156/169
  deepseek: 首 chunk 520.2 ms / 94 chunks / 总 1319.2 ms / usage 10/120/130

Ctrl-C（长提示词，启动 0.7s 后发 SIGINT）：
  glm:      2 ms，rc=130，取消前输出 8 字符
  qwen:     6 ms，rc=130，取消前输出 0 字符
  deepseek: 15 ms，rc=130，取消前输出 69 字符

key 扫描：git grep -nE 'sk-[A-Za-z0-9_.-]{16,}|[0-9a-f]{32}\.[A-Za-z0-9]{16}' → 无匹配
```

## D5：收口（2026-11-28）

- **做了什么**：警告复查；README 补编译 / 用法 / 目录 / 设计三句话；NOTES 补 D4 收口与 D5；说明书 §9 六条验收与四问自测；计划看板与现状文档同步；新增 `sdk-notes/W1-验收报告.md` 汇总真实 200、流式、取消和适配差异证据。
- **卡在哪**：无。
- **明天第一件事**：无，W1 收口；准备现场演示（见 `sdk-notes/W1-验收报告.md` 第 7 节）。

### D5 验收（六条）

```text
1. 零警告：configure_rc=0，build_rc=0，warnings=0，errors=0
2. 测试：100% tests passed, 0 tests failed out of 1
   ./build/llm_tests：fixture recorded，[case 1]~[case 4] ALL PASS
3. 流式：GLM / Qwen / DeepSeek 三家真实 200；单次实测首 chunk 594 / 568 / 520 ms，
   chunk 数 78 / 38 / 94，usage 正常（详见 W1-验收报告.md）
4. Ctrl-C：SIGINT 后 2 / 6 / 15 ms 退出，rc=130
5. key 扫描：扩展正则无匹配，scan_exit=1
6. 提交数：验收时 15（≥4）；docs 收口提交后 16（本次整理提交另计）
```

## 关键观察与坑

1. **HTTP 4xx/5xx 不是传输失败**：curl 仍返回 `CURLE_OK`，`ok=true`，状态码要单独取；错误体交给 `parseErrorBody`。
2. **响应里的 model 可能和请求不同**：DeepSeek 请求 `deepseek-chat`，响应写 `deepseek-flash`；不要拿响应 model 做一致性校验。
3. **SSE 边界比正常流更重要**：半行、半事件、跨块 CRLF、`:` 注释、多行 `data`、末尾无空行，都要有测试；逐字节重放是发现边界 bug 的最快方式。
4. **测试不要用 `assert`**：Release 的 `NDEBUG` 会把断言编掉，`ctest` 会假过；D3 改用自定义 `CHECK`，失败打印用例号 / expected / actual 并返回 1。
5. **GLM 的流式 usage 字段不同**：GLM 用 `stream_options.chunk_include_usage`，Qwen / DeepSeek 用 `include_usage`；差异放在 `usageOptionKey()` 一个钩子里，主流程不动。
6. **key 只进环境变量**：不写仓库、文档、夹具或提交；聊天里出现过的 key 建议演示后轮换。首 chunk / 总耗时都是单次实测，不能当 p50 / p95。
