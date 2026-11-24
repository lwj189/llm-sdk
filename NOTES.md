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
