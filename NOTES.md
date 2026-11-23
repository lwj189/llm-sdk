# 每日三行

## D1（2026-11-22）
- 做了什么：目录骨架 + `ir.hpp` / `cancel.hpp` / `error.hpp` + 附录原样的 CMakeLists + 6 个占位翻译单元；在真实目录 `~/桌面/sdk` 重新 configure/build 零警告，`ctest` 1/1 通过（占位 main，不含断言）。说明书与计划都在 `sdk-notes/`（随仓库提交）。
- 卡在哪：沙箱已放开，骨架已经落盘；`nlohmann-json3-dev`（3.11.3）与 `libsqlite3-dev`（3.46.1）已装好。git 仓库已建并推到 GitHub（`bdee496` 首提交、`1fbcd64` 文档整改），D1 的首次 commit 已经完成。
- 明天第一件事：D2 —— `HttpClient::post` 打通，跑通 200 / 401 / 连接失败三条路径。

### D1 验收（2026-11-23 复跑，原始输出）

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   → exit=0
cmake --build build -j                          → exit=0，warning: 0，error: 0
ctest --test-dir build --output-on-failure      → 100% tests passed, 1/1
./build/llmchat                                 → "D1 骨架，功能在 D4 实现"，exit=0
git grep -nE 'sk-[A-Za-z0-9_-]{16,}'            → 0 命中
git log --oneline                               → bdee496、1fbcd64（已推送 origin/main）
```
