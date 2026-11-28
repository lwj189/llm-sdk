# 每日三行

## D1（2026-11-22）
- 做了什么：目录骨架 + `ir.hpp` / `cancel.hpp` / `error.hpp` + 附录原样的 CMakeLists + 6 个占位翻译单元；在真实目录 `~/桌面/sdk` 重新 configure/build 零警告，`ctest` 1/1 通过（占位 main，不含断言）。说明书与计划都在 `sdk-notes/`（随仓库提交）。
- 卡在哪：沙箱已放开，骨架已经落盘；`nlohmann-json3-dev`（3.11.3）与 `libsqlite3-dev`（3.46.1）已装好。git 仓库已建并推到 GitHub（`bdee496` 首提交、`1fbcd64` 文档整改、`8603175` D1 收尾），D1 的首次 commit 已经完成。
- 明天第一件事：D2 —— `HttpClient::post` 打通，跑通 200 / 401 / 连接失败三条路径。

### D1 验收（2026-11-23 复跑，原始输出）

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   → exit=0
cmake --build build -j                          → exit=0，warning: 0，error: 0
ctest --test-dir build --output-on-failure      → 100% tests passed, 1/1
./build/llmchat                                 → "D1 骨架，功能在 D4 实现"，exit=0
git grep -nE 'sk-[A-Za-z0-9_-]{16,}'            → 0 命中
git log --oneline                               → bdee496、1fbcd64、8603175（origin/main 已同步）
```

## D2（2026-11-24）

- 做了什么：`src/http_client.cpp` 写满——`CurlState` + `on_write` / `on_progress` 两个 C 回调、全项目唯一一份 `perform()`（六项必设都在这一段）、`post()` 把响应体收进 `body_out`、`post_stream()` 逐块回调 + 取消；`examples/cli_chat.cpp` 加 6 个 `--http-smoke*` 临时驱动（D4 删）。clean build 0 警告，`ctest` 1/1。
- 卡在哪：机器上仍然没有 `DEEPSEEK_API_KEY`（Q4 / D-09 的老问题），所以「200」这条用本地 python 假服务 + 假 key 验（`LLM_SMOKE_URL` 覆盖地址），401 / fail / timeout 三条仍打真实的 `api.deepseek.com`。真实 key 的 200 等 D3 拿到 key 后补跑一次。
- 明天第一件事：D3 —— 有 key 就先录 `tests/fixtures/deepseek_stream.sse`；没 key 就按 D-09 先写解析器 + 手写夹具把 4 个用例跑通。

### D2 验收（2026-11-24 原始输出，clean build）

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   → exit=0
cmake --build build -j                          → exit=0，warning: 0，error: 0
ctest --test-dir build --output-on-failure      → 100% tests passed, 1/1

A. 200（本地假服务，LLM_SMOKE_URL=http://127.0.0.1:18080/chat/completions，DEEPSEEK_API_KEY=dummy-local-key）
[smoke-200] url=http://127.0.0.1:18080/chat/completions
[smoke-200] status=200 ok=1 elapsed_ms=1 err=""
  body.size=224
  body.head={"id": "fake-1", "object": "chat.completion", "choices": [{"index": 0, "message": {"role": "assistant", "content": "pong"} ...
[smoke-200] PASS

B. stream（同一个本地服务，chunked SSE 5 块、每块隔 0.4s → 真的是逐块回调）
[smoke-stream] status=200 ok=1 blocks=5 bytes=228 elapsed_ms=2002 err=""
[smoke-stream] head(200)=data: {"choices":[{"delta":{"content":"你好"}}]}
[smoke-stream] PASS

C. cancel（收到第一块就把取消位置 1，本次回调仍返回 true）
[smoke-cancel] status=200 ok=0 cancelled=1 bytes=52 elapsed_ms=1
[smoke-cancel] err="Operation was aborted by an application callback (CURLcode 42)"
[smoke-cancel] PASS

D. 401（真实 api.deepseek.com，Authorization: Bearer invalid）
[smoke-401] status=401 ok=1 err=""          ← 传输层成功，所以 ok=1
  body.size=204
  body.head={"error":{"message":"Authentication Fails, Your api key: ****alid is invalid ...
[smoke-401] PASS

E. fail（127.0.0.1:1）
[smoke-fail] status=0 ok=0 err="Could not connect to server (CURLcode 7)"
[smoke-fail] PASS

F. timeout（connect_timeout_ms=1）
[smoke-timeout] status=0 ok=0 elapsed_ms=5 err="Timeout was reached (CURLcode 28)"
real	0m0.009s
[smoke-timeout] PASS

G. key 扫描
grep -rnE "sk-[A-Za-z0-9_-]{16,}" → 0 命中
```

## D3（2026-11-25）

- 做了什么：`include/llm/sse.hpp` + `src/sse.cpp` 实现 SSE 解析器——`feed` 行扫描、半行/半事件挂起、跨块 CRLF、`:` 注释、多行 `data`、`finish` 幂等；`tests/test_sse.cpp` 四个用例（整读 / 逐字节重放 / 混合边界 / 残行 finish）；新增手写夹具 `tests/fixtures/handwritten_stream.sse`；按说明书 §8.1 用真实 key 录 `tests/fixtures/deepseek_stream.sse`（32494 字节 / 236 行 / 118 条 `data` / 117 条 JSON chunk + `[DONE]`）。clean build 0 警告，`ctest` 1/1，真实夹具四例全 PASS；Release（`NDEBUG`）构建也同样 0 警告、`ctest` 1/1、四例 PASS（自定义 CHECK 不会被编掉）。
- 卡在哪：原卡点是机器上没有 `DEEPSEEK_API_KEY`，真实夹具无法录；现已用真实 key 录到夹具并重跑通过，D3 没有遗留问题。
- 明天第一件事：D4 —— 先建 `include/llm/deepseek.hpp`，再填 `src/providers/openai_compat.cpp` 的 `buildBody` / `chat` / `chatStream`，最后接流式 CLI（计划 §5）。

> 真实夹具观察：响应里的 `model` 是 `deepseek-flash`（请求写的是 `deepseek-chat`）；D4 不要拿响应 model 做一致性校验。

### D3 验收（2026-11-25 原始输出，clean build）

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

git grep -nE 'sk-[A-Za-z0-9_-]{16,}'            → 0 命中
ls tests/fixtures                               → deepseek_stream.sse（真实，32494 字节）、handwritten_stream.sse（手写回退）
```

## D4（2026-11-26）

- 做了什么：新增 `include/llm/provider.hpp`（`LLMProvider` + 模板方法式 `OpenAICompatProvider`）与 `include/llm/deepseek.hpp`；填满 `src/providers/openai_compat.cpp` 的 `endpoint` / `apiKey` / `isAvailable` / `buildBody` / `chat` / `chatStream` / `parseChunk` / `parseErrorBody`；`src/providers/deepseek.cpp` 填 DeepSeek 默认值；`examples/cli_chat.cpp` 改成真正的流式 CLI（逐字打印、reasoning 打到 stderr、usage、SIGINT → `CancelToken`、退出码 130），按 D-01 删除全部 `--http-smoke*`。
- 卡在哪：手上这把 DeepSeek key 已失效（真实请求 401 `Authentication Fails ... is invalid`），所以真实 200 还没验；本地 mock 的流式 / 非流式 / 401 / 取消四条已全 PASS，真实 401 也验过。拿到新 key 后补跑 `./build/llmchat "用三句话解释 epoll"` 与 Ctrl-C 两条，D4 才算完整。
- 明天第一件事：D5 收口；先补 D4 的真实 200，再按说明书 §9 六条验收 + 四问自测，最后 `docs:` commit。

### D4 验收（2026-11-26 原始输出，clean build；本地部分）

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

本地 mock（/tmp/mock_deepseek.py，127.0.0.1:18080）：
  nonstream ok=1 content=你好，世界 usage=3/4/7
  stream ok=1 chunks=117 done=1 usage=1 len=459 text_head=1. epoll 是 Linux ...
  401 ok=0 len=0
  401-chat ok=0
  sink-false ok=0 chunks=1 cancelled=1
  cancel ok=0 chunks=2 cancelled=1 elapsed_ms=200
  ALL PASS

临时 CLI 指向本地 mock：
  /slow 上 0.5s 时 Ctrl-C → rc=130，约 500ms 回到 shell，stdout "012"，stderr "[错误] 请求失败: 请求已取消"
  /fast 上 rc=0，stderr "[usage] prompt=10 completion=115 total=125"、"[done] 5455 ms"

git grep -nE 'sk-[A-Za-z0-9_-]{16,}'            → 0 命中
```

## D4 扩展：GLM / 千问 provider（2026-11-28）

- 做了什么：新增 include/llm/glm.hpp、include/llm/qwen.hpp 与 src/providers/glm.cpp、src/providers/qwen.cpp；OpenAICompatProvider 增加 usageOptionKey() 钩子（默认 include_usage，GLM 覆盖为 chunk_include_usage）；CLI 增加 --provider glm|qwen|deepseek 与 --model，默认 glm，保留 DeepSeek；GLM key 回退 ZHIPUAI_API_KEY，千问回退 QWEN_API_KEY；可选 GLM_BASE_URL / QWEN_BASE_URL 便于本地 mock。
- 卡在哪：GLM key 真实请求返回 HTTP 401「身份验证失败」（标准端点与 coding 端点、glm-4-flash / glm-4.6 / glm-4.5 都试过，均为 401）；千问没有 key；DeepSeek 旧 key 也失效。真实 200 仍未验，D4 保持 🟡。
- 明天第一件事：拿到有效 GLM 或千问 key 后，先跑真实 200 与 Ctrl-C 两条；再补 D4 DeepSeek 200 和 D5。

### D4 扩展验收（2026-11-28 原始输出）

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

真实 GLM（key 只放临时进程环境，未写盘、未打印）：
  [provider] glm
  [错误] 请求失败（HTTP 401） code=1000: 身份验证失败。
  [耗时] 334 ms

git grep -nE 'sk-[A-Za-z0-9_-]{16,}'            → 0 命中
```
