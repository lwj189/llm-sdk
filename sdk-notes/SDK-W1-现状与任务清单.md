# 大模型接入 SDK（W1）—— 现状说明 + 任务清单

> 写于 2026-11-22。读者：李文杰本人 + 下一个接手的 AI 会话。
> 唯一权威是规格全文 `交接-SDK-W1.md`；本文件只做两件事：记录**已经发生的事实**，把**要做的事**列成可勾的清单。
> ⚠ 本文件暂时落在 `/home/aaa/桌面/server/` 下，因为这个会话只能写这个目录。它应被移到 `/home/aaa/桌面/llm-sdk/`（命令见第 6 节），移走后 server 仓库仍是干净的（现在是 untracked，未提交）。

---

## 0. 一句话现状

规格读完了、环境自检过了、**D1 骨架已经在临时目录里编译通过（0 警告）**；
**但项目目录还不存在，所以一行代码都还没落盘** —— Agent 的文件写权限只有 `/home/aaa/桌面/server`，而项目要建在 `/home/aaa/桌面/llm-sdk`。

卡点只有一个：**目录 + 写权限**。不是技术问题。

---

## 1. 这个项目要做什么

一句话：**不用任何官方 SDK，只用 libcurl 自己实现 HTTP + SSE 层，把「用户消息」翻译成「模型逐字返回的流」。**

三个真实目的（按重要性）：

1. **替换简历上的减分项**：现在是 Node.js + Express + Electron 的笔记应用，技术栈不对口，还自我否定（"全部编码由 AI 辅助完成"）。必须换掉。
2. **补两块现在没有的能力**：抽象设计（纯虚基类 + 派生 + 钩子函数）、第三方 C 库集成（C 回调 ↔ C++ 对象）。这是目前最大的缺口。
3. **产生可验证的数字**：首 token 延迟 / 端到端耗时 / 取消延迟 / p50 / p95，W4 用脚本从原始日志算。

**不做产品，做「能讲清、能验证、有数字」的库。**

---

## 2. 硬约束（违反任何一条都会被退回）

| # | 约束 | 说明 |
| --- | --- | --- |
| 1 | 只在 Ubuntu 虚拟机里开发 | 不写 Windows 构建，不碰 MSVC / vcpkg / 多平台 CMakePresets |
| 2 | C++17 | 不碰 C++20（协程 / concepts / ranges / modules） |
| 3 | 依赖只用 apt + header-only | 禁 conan / vcpkg / FetchContent；W1 只允许 libcurl + nlohmann/json |
| 4 | 只接国产模型 | DeepSeek / 智谱 GLM / 通义 Qwen，不接需要梯子的 |
| 5 | 每天只有 1.5~2 小时 | 每次提交要小、能独立回滚，不许一口气 800 行 |
| 6 | 不能提交任何 API key | key 只从环境变量读，`.env` 必须在 `.gitignore` |
| 7 | 不碰服务器项目 | `~/桌面/server` 那个 epoll 服务器与此无关，不要"顺手优化" |

---

## 3. 要做的东西（模块清单）

| 文件 | 层 | 负责什么 | 计划 | 当前状态 |
| --- | --- | --- | --- | --- |
| `include/llm/ir.hpp` | 抽象 | 与厂商无关的对话数据：Role / Message / ChatRequest / StreamChunk / ChatResult | D1 | 内容已定，未落盘 |
| `include/llm/cancel.hpp` | 基础设施 | 跨线程取消标志（`atomic<bool>`） | D1 | 同上（有一处偏差，见第 5 节） |
| `include/llm/error.hpp` | 基础设施 | `ProviderError`：http_status / 厂商 code / message / cancelled | D1 | 同上 |
| `include/llm/sse.hpp` + `src/sse.cpp` | 传输 | 字节流 → `SSEEvent`；**不认识 JSON，也不认识 `[DONE]`** | D3 | 未开始 |
| `include/llm/http_client.hpp` + `src/http_client.cpp` | 传输 | libcurl 封装：`post` / `post_stream` 共用一份 curl 配置代码 | D2 | 未开始 |
| `include/llm/provider.hpp` | 协议 | `LLMProvider` 纯虚基类 + `OpenAICompatProvider`（模板方法，主流程 final） | D4 | 未开始 |
| `src/providers/openai_compat.cpp` | 协议 | 主流程：建 body → post_stream → SSEParser → parseChunk → 回调 → 判终止 | D4 | 未开始 |
| `src/providers/deepseek.cpp` | 协议 | 只填默认值（base_url / key 环境变量名 / 默认模型） | D4 | 未开始 |
| `examples/cli_chat.cpp` | 应用 | 流式 CLI：参数 → chatStream → 逐字打印；Ctrl-C 1 秒退出 | D4 | 3 行占位 |
| `tests/test_sse.cpp` + `tests/fixtures/deepseek_stream.sse` | 测试 | 4 个用例：整体喂入 / 逐字节重放 / 混合边界 / 残块 | D3 | 占位（恒返回 0） |
| `CMakeLists.txt` | 构建 | 附录版本原样：`-Wall -Wextra -Wpedantic`，CURL + nlohmann 兜底 | D1 | 内容已定，未落盘 |
| `README.md` / `NOTES.md` / `.gitignore` | 文档 | 用法 + 每天三行（做了什么 / 卡在哪 / 明天第一件事） | D1 / D5 | 内容已定，未落盘 |

**目录结构**（照规格第 5 节，一字不差）：

```text
llm-sdk/
├── CMakeLists.txt
├── .gitignore
├── README.md
├── NOTES.md
├── include/llm/{ir,cancel,error,sse,http_client,provider}.hpp
├── src/{sse.cpp,http_client.cpp}
├── src/providers/{openai_compat.cpp,deepseek.cpp}
├── examples/cli_chat.cpp
├── tests/test_sse.cpp
├── tests/fixtures/deepseek_stream.sse
└── third_party/nlohmann/          # 只有 apt 装不上 json 时才放单头文件
```

---

## 4. 五天计划与验收

| Day | 产出 | 验收（必须贴真实输出） | 状态 |
| --- | --- | --- | --- |
| **D1** | 目录骨架 + `ir/cancel/error` 三个头 + CMakeLists + 占位翻译单元 + `.gitignore` + `NOTES.md` + 首次 commit | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j` 通过 | **已验证，未落盘** |
| **D2** | `HttpClient::post` 打通；`post_stream` 先只打印字节数 | 能打印 HTTP 200 + body 前 200 字符；401 能打印厂商原文；`127.0.0.1:1` 能返回 curl 描述 | 未开始 |
| **D3** | `sse.cpp` 的 feed/finish/reset；录夹具；4 个用例 | `ctest --test-dir build --output-on-failure` 全过（**逐字节重放不过就别往下做**） | 未开始 |
| **D4** | `openai_compat.cpp` 主流程 + `deepseek.cpp` 默认值 + 流式 CLI + SIGINT | 字**一个一个**蹦出来（不是等 3 秒一次性出现）；Ctrl-C 立刻回 shell | 未开始 |
| **D5** | 警告清零 + README + NOTES + ≥4 个有意义的 commit | 下面 6 条全过 | 未开始 |

**W1 总交付 6 条验收**：

```bash
cmake -S . -B build && cmake --build build -j 2>&1 | grep -c "warning:"   # 期望 0
ctest --test-dir build --output-on-failure                               # 期望 100% passed
DEEPSEEK_API_KEY=sk-xxx ./build/llmchat "你好"                            # 人工看：逐字
# Ctrl-C 人工看：1 秒内回到 shell
git grep -n "sk-" ; echo "exit=$?"                                       # 期望无匹配
git log --oneline | wc -l                                                # 期望 >= 4
```

---

## 5. 已经发生的事实（可直接引用）

### 5.1 环境自检（2026-11-22 实测）

| 项 | 实测 | 判据 | 结论 |
| --- | --- | --- | --- |
| OS | Ubuntu 26.04.1 LTS | — | — |
| g++ | 15.2.0 | ≥ 9 | ✅ |
| cmake | 4.2.3 | ≥ 3.16 | ✅（4.x 仍接受 3.16 的 `cmake_minimum_required`） |
| curl | 8.18.0 | — | — |
| libcurl（dev） | 8.18.0-1ubuntu2.8 | ≥ 7.58 | ✅ |
| `curl https://api.deepseek.com` | HTTP **401** | 拿到响应即通 | ✅ 网络通 |
| nlohmann/json | **未安装** | apt 里有 3.11.3 | ⬜ 待装 |
| libsqlite3-dev | 未安装 | W3 才用 | ⬜ 待装 |
| libssl-dev | 3.5.5 已装 | — | ✅ |
| 机器 | 4 vCPU / 15 GiB | — | 与压测 VM 同一台 |

### 5.2 D1 骨架验证结果

在临时目录里按附录 CMakeLists 原样建骨架后编译：

```text
-- Found CURL: /usr/lib/x86_64-linux-gnu/libcurl.so (found version "8.18.0")
configure_exit=0    build_exit=0    warning 计数 = 0
1/1 Test #1: sse ... Passed    100% tests passed
-rw-rw-r--  libllmcore.a   -rwxrwxr-x  llmchat   -rwxrwxr-x  llm_tests
```

注意：这次 `ctest` 通过是**占位 main 恒返回 0**，不代表任何断言（真用例在 D3）。

**D1 的 13 个文件**：`CMakeLists.txt`、`.gitignore`、`README.md`、`NOTES.md`、`include/llm/ir.hpp`、`include/llm/cancel.hpp`、`include/llm/error.hpp`、`src/sse.cpp`、`src/http_client.cpp`、`src/providers/openai_compat.cpp`、`src/providers/deepseek.cpp`、`examples/cli_chat.cpp`、`tests/test_sse.cpp`（+ `tests/fixtures/.gitkeep`、`third_party/nlohmann/.gitkeep`）。

### 5.3 对规格的两处处理（需要你确认）

1. **`cancel.hpp` 的实现内联进类内**。规格给的目录结构里**没有 `cancel.cpp`**，而 `void cancel(); bool cancelled() const;` 只有声明会链接失败。所以改成类内定义：
   ```cpp
   void cancel() { flag_.store(true, std::memory_order_relaxed); }
   bool cancelled() const { return flag_.load(std::memory_order_relaxed); }
   ```
   签名与语义（跨线程安全、只读查询）没变，只是定义位置变了。
2. **6 个 `.cpp` 先写 1~3 行占位**。附录 CMakeLists 直接列了 `src/sse.cpp` 等 4 个源文件、`llmchat`、`llm_tests`，但规格又要求 D1「只有头文件也能构建」。占位翻译单元是同时满足两者的最小代价（每个文件头注释写明"哪一天填什么"）。

### 5.4 还没做、也不该现在做的事

- **git 仓库没有建**（你说的"仓库我后面自己搞"），所以 D1 的首次 commit 是空的；D5 的「≥4 个 commit」需要在你建库后逐日补。
- 规格全文目前**只存在于聊天记录**里。落盘后应存成 `llm-sdk/交接-SDK-W1.md`，否则下个会话要重新贴一遍。

### 5.5 顺手清理过的痕迹

调试时误在 `~/桌面/server/build/` 下生成了 `Testing/` 目录（ctest 的残留），已删除；`server` 仓库 `git status` 仍然干净（HEAD `c133dac`）。

---

## 6. 现在要做的两步（卡点解除）

```bash
# 1) 建项目目录（顺便把本文件搬进去）
mkdir -p /home/aaa/桌面/llm-sdk
mv /home/aaa/桌面/server/SDK-W1-现状与任务清单.md /home/aaa/桌面/llm-sdk/

# 2) 装缺的两个开发包
sudo apt install -y nlohmann-json3-dev libsqlite3-dev
```

然后把**本会话的工作目录切到 `/home/aaa/桌面/llm-sdk`**（GUI 切不了就在该目录开新会话，把规格第 13 节那段提示词贴过去）。

替代方案（更快）：**批准一次「工作区外写权限」**，Agent 直接把 13 个文件写进 `~/桌面/llm-sdk`，并顺手把规格存成 `交接-SDK-W1.md`。

---

## 7. 设计红线（面试四问的根，改代码时不许动）

1. `virtual ~LLMProvider() = default;` —— 派生类有自己的 `std::string` 成员，不 virtual 就会漏析构。
2. `chatStream` 用 `std::function` 收回调，而不是再加一个虚函数 —— **"拿到数据之后干什么"该由调用方（CLI / 将来的 W2 网关）决定**。
3. `OpenAICompatProvider` 把主流程写 `final`，只留 5 个 `protected` 虚函数（`endpoint` / `apiKey` / `buildBody` / `parseChunk` / `isDoneMark`，外加 `parseErrorBody`）—— 模板方法：换厂商只覆盖钩子，主流程只有一份。
4. `ChatRequest` / `Message` 里**不许出现任何厂商字段**（`stream_options`、`reasoning_effort` 这类只能活在 `buildBody()` 里）。
5. 加一个**完全不兼容 OpenAI 协议的厂商**时，答案必须是「加一个新类，主流程一行 `if/else` 都不改」。做不到就说明抽象漏了。

---

## 8. 最容易写错的 8 条（踩过就不算坑）

1. `CURLOPT_NOPROGRESS, 0L` 不设 → 进度回调永不触发 → **取消功能"看起来实现了但不生效"**。
2. 回调必须是**静态/自由函数 + userdata**；成员函数传不进 `CURLOPT_WRITEFUNCTION`。
3. 超时一律用毫秒版 `CURLOPT_CONNECTTIMEOUT_MS` / `CURLOPT_TIMEOUT_MS`（秒版精度不够，测试没法写）。
4. SSE 必须**同时认 CRLF 和 LF**；只认 `\n\n` 在真实流量上一个事件都切不出来，且症状是「静默无输出」。
5. `data:` 后只剥**一个**空格（正文里的空格有意义）；多行 data 用单个 `\n` 拼接；`: ` 开头是注释（心跳），不产生事件也不清空当前事件。
6. `data: [DONE]` **先判 `isDoneMark()` 再解析 JSON**，否则第 0 字节就报 parse error。
7. 用 `std::getline` 读响应体 → 流式下会等，看起来像卡死。必须用写回调。
8. 录夹具必须带 `-N --http1.1`，否则录出来是空的或只有一行。

---

## 9. 现在不要做的事（越界清单）

❌ GPT / Gemini / Responses API ❌ SMTP 邮件验证码 ❌ Web 前端 / 注册登录
❌ spdlog / fmt / gflags / jsoncpp ❌ provider registry（W1 只有一个 provider）
❌ function calling / 多模态 / 图片 / 语音 ❌ SQLite / OpenSSL（W3）
❌ Windows 构建 / 跨平台 ifdef ❌ 改 `~/桌面/server` 那个 epoll 服务器

---

## 10. 下个会话从哪开始

1. 读 `交接-SDK-W1.md`（规格）+ 本文件（现状）。
2. 确认第 6 节两步已做完（目录存在、`pkg-config --modversion libcurl` 与 `ls /usr/include/nlohmann/json.hpp` 都有输出）。
3. 从 **D1 落盘**开始：13 个文件 + 一次 commit，然后立刻跑 `cmake -S . -B build && cmake --build build -j` 贴真实输出。
4. 一天一停，等确认再进下一天；每写完一个文件用中文说三句：负责什么 / 为什么这么拆 / 最容易写错哪里。
