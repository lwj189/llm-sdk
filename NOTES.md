# 每日三行

## D1（2026-11-22）
- 做了什么：目录骨架 + `ir.hpp` / `cancel.hpp` / `error.hpp` + 附录原样的 CMakeLists + 6 个占位翻译单元；在真实目录 `~/桌面/sdk` 重新 configure/build 零警告，`ctest` 1/1 通过（占位 main，不含断言）。规格、计划与旧清单都在 `sdk-notes/`（随仓库提交）。
- 卡在哪：沙箱已放开，骨架已经落盘；`nlohmann-json3-dev`（3.11.3）与 `libsqlite3-dev`（3.46.1）已装好。**git 仓库还没建**（本人来建），所以 D1 的首次 commit 仍是空的。
- 明天第一件事：D2 —— `HttpClient::post` 打通，跑通 200 / 401 / 连接失败三条路径。
