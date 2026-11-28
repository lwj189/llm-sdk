# llm-sdk W1 项目笔记

> 项目：llm-sdk —— 不用官方 SDK，只用 libcurl 手写 HTTP + SSE 的 C++17 大模型接入库。
> 状态：W1 已完成（D1–D5，2026-11-28）；DeepSeek / GLM / 千问三家真实 HTTP 200 已验收。
> 验收提交：`cdc804c`（docs 收口）；代码提交 `f48546f`。当前 HEAD 以 `git log -1` 为准。

## 0. 五分钟了解现状

> 最后更新：2026-11-28。W1 已完成（D1–D5）；DeepSeek / GLM / 千问三家真实 HTTP 200 已验收。
> 本文是**单文件项目笔记**：原 `NOTES.md` 与 `sdk-notes/` 下的说明书、W1 实施计划、验收报告、项目现状已合并到这里；git 历史里仍可查旧版本。
> §1–§11 沿用原说明书的章节编号；代码注释里的「说明书 §7.3」对应的就是本文同一节。

### 0.1 先跑几条命令（30 秒）

~~~bash
cd ~/桌面/sdk
git log --oneline -5
git status -sb
cmake --build build -j 2>&1 | grep -c "warning:"
ctest --test-dir build --output-on-failure | tail -3
~~~

判读：

| 看到什么 | 说明 |
| --- | --- |
| 最新 commit 是 `docs:` / `chore:` 开头 | 只是收尾，代码进度看前一条 |
| `## main...origin/main` 没有 ahead / behind | 本地和 GitHub 同步（origin 走 ssh:443） |
| `warning:` 计数 0、`100% tests passed` | 验收第 1、2 条还立着 |

### 0.2 一句话状态

W1 已完成（D1–D5）。同一套 `LLMProvider` / `OpenAICompatProvider` 已接三家；`./build/llmchat --provider glm|qwen|deepseek "问题"` 能逐字流式输出，`[DONE]` 早停、HTTP 错误体、非流式 `chat()`、SIGINT 取消都覆盖。clean build 0 警告、`ctest` 1/1，SSE 真实夹具四用例全 PASS。

三家真实 200 已验：GLM / 千问 / DeepSeek 均返回 HTTP 200，首 chunk 约 520–594 ms，usage 正常；Ctrl-C 2 / 6 / 15 ms 回 shell（rc=130）。完整命令、原始输出与四问自测见 §15。

### 0.3 读这份笔记的顺序

1. §0：先确认仓库状态（上面几条命令）。
2. §1–§11：项目是什么、接口契约、实现红线、测试与验收标准。
3. §12–§13：每天怎么拆、关键决定与理由。
4. §14：D1–D5 的实际执行记录与原始输出。
5. §15：最终验收报告、三家真实 200、Ctrl-C、四问自测、现场演示。

### 0.4 代码入口

| 想知道 | 看哪 |
| --- | --- |
| 数据结构（Message / ChatRequest / StreamChunk） | `include/llm/ir.hpp` |
| 取消怎么传 | `include/llm/cancel.hpp` |
| 错误怎么报 | `include/llm/error.hpp` |
| 每天新建的头文件 | `http_client.hpp`(D2) → `sse.hpp`(D3) → `provider.hpp` + `deepseek.hpp`(D4) → `glm.hpp` + `qwen.hpp` |
| HTTP 实现 | `src/http_client.cpp`（`perform()` 六项必设） |
| SSE 实现 | `src/sse.cpp`（行扫描 + 半行/半事件挂起） |
| Provider 主流程 | `src/providers/openai_compat.cpp`（buildBody / chat / chatStream） |
| 三家默认值 | `src/providers/deepseek.cpp`、`glm.cpp`、`qwen.cpp` |

### 0.5 每次收工必须做的

1. 在 §14 对应的那天补三行（做了什么 / 卡在哪 / 明天第一件事）。
2. 跑当天验收命令，把原始输出贴进 §14，不许只写「已完成」。
3. `git add -A && git commit && git push`，确认 `git status -sb` 没有 ahead。

## 1. 这个项目是什么

三个目的，按重要性排：

1. **替换简历上的减分项**：原来那个 Node.js + Express + Electron 的笔记应用技术栈不对口。
2. **补两块现在没有的能力**：抽象设计（纯虚基类 + 派生 + 模板方法）、第三方 C 库集成（C 回调 ↔ C++ 对象）。
3. **产生可验证的数字**：首 token 延迟 / 端到端耗时 / 取消延迟 / p50 / p95。

**不追求做成产品，追求的是「能讲清、能验证、有数字」的库。**

## 2. 硬约束

| # | 约束 | 说明 |
| --- | --- | --- |
| 1 | 只在 Ubuntu 虚拟机开发 | 不写 Windows 构建，不碰 MSVC / vcpkg / CMakePresets |
| 2 | C++17 | 不碰 C++20（协程 / concepts / ranges / modules） |
| 3 | 依赖只用 apt + header-only | 禁 conan / vcpkg / FetchContent；W1 只用 libcurl + nlohmann/json |
| 4 | 只接国产模型 | 本轮接 DeepSeek / GLM / 千问，均走 OpenAI 兼容协议 |
| 5 | 每天只有 1.5~2 小时 | 所以按天切分，每段 25~40 分钟，每段结束必须能编译 |

## 3. 目录与文件

```text
llm-sdk/                      # 本地目录名 ~/桌面/sdk，GitHub 仓库名 llm-sdk
├── CMakeLists.txt            # 3 个目标：llmcore(静态库) / llmchat(CLI) / llm_tests(测试)
├── .gitignore                # build/ .env *.o *.log compile_commands.json
├── README.md                 # 对外简介 + 怎么编译（D5 补全）
├── NOTES.md                  # 单文件项目笔记：说明 / 计划 / 日志 / 验收
├── include/llm/              # ← 对外接口，别人 include 的就是这里
│   ├── ir.hpp                # ✅ 统一数据结构（Role/Message/ChatRequest/StreamChunk/Usage）
│   ├── cancel.hpp            # ✅ CancelToken（atomic<bool>，跨线程安全）
│   ├── error.hpp             # ✅ ProviderError（http_status/code/message/cancelled）
│   ├── sse.hpp               # ✅ D3：SSEParser
│   ├── http_client.hpp       # ✅ D2：HttpClient
│   ├── provider.hpp          # ✅ D4：LLMProvider + OpenAICompatProvider
│   ├── deepseek.hpp          # ✅ D4：DeepSeekProvider
│   ├── glm.hpp               # ✅ D4 扩展：GlmProvider
│   └── qwen.hpp              # ✅ D4 扩展：QwenProvider
├── src/
│   ├── http_client.cpp       # ✅ D2：libcurl 封装（post / post_stream）
│   ├── sse.cpp               # ✅ D3：SSE 状态机
│   └── providers/
│       ├── openai_compat.cpp # ✅ D4：模板方法主流程
│       ├── deepseek.cpp      # ✅ D4：DeepSeek 默认值
│       ├── glm.cpp           # ✅ D4 扩展：GLM 默认值 + chunk_include_usage
│       └── qwen.cpp          # ✅ D4 扩展：千问默认值
├── examples/cli_chat.cpp     # ✅ D4：流式 CLI（--provider glm|qwen|deepseek）
├── tests/
│   ├── test_sse.cpp          # ✅ D3：4 个用例（自定义 CHECK）
│   └── fixtures/             # ✅ D3 真实夹具 + 手写回退
└── build/                    # 编译产物，已 gitignore，随时可删
```

✅ = 已定稿；W1 结束时没有占位实现。

## 4. 构建、运行、测试

```bash
# 配置 + 编译（要求 0 警告）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2>&1 | grep -c "warning:"      # 期望 0

# 单元测试
ctest --test-dir build --output-on-failure            # 期望 100% passed

# 跑 CLI（D4 之后；默认 provider 是 glm）
export GLM_API_KEY=你的key
./build/llmchat "用三句话解释 epoll"

# 切千问 / DeepSeek
DASHSCOPE_API_KEY=... ./build/llmchat --provider qwen "你好"
DEEPSEEK_API_KEY=... ./build/llmchat --provider deepseek "你好"
```

CMake 目标：

| 目标 | 产物 | 来源 |
| --- | --- | --- |
| `llmcore` | `libllmcore.a` | `src/` 下 6 个 `.cpp`（http_client、sse + providers 下 4 个） |
| `llmchat` | 可执行 | `examples/cli_chat.cpp` |
| `llm_tests` | 可执行 | `tests/test_sse.cpp`（`add_test(NAME sse ...)`） |

## 5. 分层与数据流

```text
cli_chat.cpp
  └─ LLMProvider::chatStream(req, on_chunk, cancel, err)      ← 纯虚基类
       └─ OpenAICompatProvider                                 ← 主流程 final
            ├─ DeepSeekProvider / GlmProvider / QwenProvider      ← 只改默认值与少量钩子
            ├─ buildBody()             IR(结构体) → 厂商 JSON
            ├─ HttpClient::post_stream()  → libcurl 写回调，逐块吐字节
            ├─ SSEParser::feed()          字节 → SSEEvent
            ├─ parseChunk()               SSEEvent(JSON) → StreamChunk
            ├─ on_chunk(StreamChunk)      → CLI 逐字打印
            └─ isDoneMark()               data == "[DONE]" → 结束
```

两条红线：

- **SSEParser 不许知道 JSON，也不许知道 `[DONE]`** —— 它只把字节切成事件；认识 `[DONE]` 的是 provider。
- **Provider 不许直接碰 curl** —— 它只调 `HttpClient`，换 HTTP 实现时不用改业务。

## 6. 接口契约（签名照抄，不要「顺手优化」）

### 6.1 `ir.hpp` / `cancel.hpp` / `error.hpp`

已在仓库里且已定稿，改动等于改对外契约。要点：

- `ChatRequest`：`model` / `messages` / `temperature=0.7` / `max_tokens=1024` / `stream=true`
- `StreamChunk`：`text`（正文增量）、`reasoning`（思维链增量）、`finish_reason`、`done`、`has_usage` + `usage`
- `ProviderError`：`http_status`（0 = 没走到 HTTP）、`code`、`message`、`cancelled`

### 6.2 `sse.hpp`（D3 新建）

```cpp
struct SSEEvent {
    std::string event;  // event: 字段，缺省为空
    std::string id;     // id: 字段
    std::string data;   // 多行 data 用单个 \n 拼接，末尾不带换行
};

class SSEParser {
public:
    std::vector<SSEEvent> feed(const char* p, std::size_t n);  // 任意切断都能解析
    std::vector<SSEEvent> finish();                            // 吐出缓冲区残块
    void reset();
private:
    std::string buf_;
    // 允许增加私有成员（例如「当前正在拼的事件」），公开签名不变
};
```

### 6.3 `http_client.hpp`（D2 新建）

```cpp
struct HttpRequest {
    std::string url;
    std::vector<std::string> headers;   // "Content-Type: application/json"
    std::string body;
    long connect_timeout_ms = 5000;
    long total_timeout_ms   = 120000;
    long low_speed_limit    = 1;        // 字节/秒；0 = 不启用
    long low_speed_time_s   = 30;
};

class HttpClient {
public:
    using ByteSink = std::function<bool(const char*, std::size_t)>;

    static void globalInit();     // main 开头调一次（curl_global_init）
    static void globalCleanup();

    bool post(const HttpRequest& req, std::string* body_out,
              long* http_status, std::string* err);

    bool post_stream(const HttpRequest& req, const ByteSink& on_bytes,
                     const CancelToken& cancel,
                     long* http_status, std::string* err);
};
```

约束：`post` 与 `post_stream` **共用一份实现**，不要复制两遍 curl 配置。返回 `false` 时 `err` 必须有内容；`http_status` 尽量用 `CURLINFO_RESPONSE_CODE` 填（即使 perform 失败也取一次）。

### 6.4 `provider.hpp`（D4 新建）

```cpp
class LLMProvider {
public:
    virtual ~LLMProvider() = default;          // 必须 virtual

    virtual std::string name() const = 0;
    virtual bool isAvailable(std::string* why) const = 0;   // key 是否在

    virtual bool chat(const ChatRequest& req, ChatResult* out,
                      ProviderError* err) = 0;

    using ChunkSink = std::function<bool(const StreamChunk&)>;
    virtual bool chatStream(const ChatRequest& req, const ChunkSink& on_chunk,
                            const CancelToken& cancel,
                            ProviderError* err) = 0;
};

class OpenAICompatProvider : public LLMProvider {
public:
    OpenAICompatProvider(std::string name, std::string base_url,
                         std::string api_key_env, std::string default_model);

    std::string name() const final;
    bool isAvailable(std::string* why) const final;
    bool chat(const ChatRequest&, ChatResult*, ProviderError*) final;
    bool chatStream(const ChatRequest&, const ChunkSink&,
                    const CancelToken&, ProviderError*) final;

protected:
    virtual std::string endpoint() const;                     // 完整 URL
    virtual std::string apiKey() const;
    virtual std::string usageOptionKey() const;               // 流式 usage 开关字段名
    virtual std::string buildBody(const ChatRequest&) const;   // IR → 厂商 JSON
    virtual bool parseChunk(const SSEEvent& ev, StreamChunk* out,
                            ProviderError* err) const;
    virtual bool isDoneMark(const SSEEvent& ev) const;         // 默认 data == "[DONE]"
    virtual bool parseErrorBody(const std::string& body,
                                ProviderError* err) const;     // 4xx/5xx
};
```

**四个 `final` 加一个 `virtual ~LLMProvider()` —— 这是本项目最值钱的几行，别改形状。** 新增厂商 = 加一个派生类 + 改默认值，主流程一行不动。

### 6.5 `deepseek.hpp` / `glm.hpp` / `qwen.hpp`（D4 新建）

```cpp
class DeepSeekProvider : public OpenAICompatProvider {
public:
    DeepSeekProvider();   // base_url=https://api.deepseek.com, env=DEEPSEEK_API_KEY, model=deepseek-chat
};

class GlmProvider : public OpenAICompatProvider {
public:
    GlmProvider();        // base_url=https://open.bigmodel.cn/api/paas/v4, env=GLM_API_KEY, model=glm-4-flash
protected:
    std::string apiKey() const override;          // 回退 ZHIPUAI_API_KEY
    std::string usageOptionKey() const override;  // "chunk_include_usage"
};

class QwenProvider : public OpenAICompatProvider {
public:
    QwenProvider();       // base_url=https://dashscope.aliyuncs.com/compatible-mode/v1, env=DASHSCOPE_API_KEY, model=qwen-plus
protected:
    std::string apiKey() const override;          // 回退 QWEN_API_KEY
};
```

## 7. 实现红线

### 7.1 libcurl 六项必设（漏一个就会莫名挂起）

1. `CURLOPT_NOSIGNAL, 1L` —— 多线程下必须，否则 DNS 超时用 alarm 信号
2. `CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1` —— 强制 1.1（SSE 在 h2 下行为不同，W1 不处理）
3. `CURLOPT_WRITEFUNCTION` + `CURLOPT_WRITEDATA` —— 回调必须是**静态/自由函数**，`this` 走 userdata
4. `CURLOPT_NOPROGRESS, 0L` + `CURLOPT_XFERINFOFUNCTION` —— 不设 0，进度回调永不触发，取消就失效
5. 超时用毫秒版：`CURLOPT_CONNECTTIMEOUT_MS` / `CURLOPT_TIMEOUT_MS`
6. `CURLOPT_TCP_KEEPALIVE, 1L` —— 长连接保活

### 7.2 C 回调 ↔ C++ 对象（唯一容易卡死的地方）

```cpp
struct CurlState {
    const HttpClient::ByteSink* sink = nullptr;
    const CancelToken* cancel = nullptr;
    std::string* body = nullptr;
};

static std::size_t on_write(char* ptr, std::size_t size, std::size_t nmemb, void* ud) {
    auto* st = static_cast<CurlState*>(ud);
    const std::size_t n = size * nmemb;
    if (st->sink && *st->sink) {
        if (!(*st->sink)(ptr, n)) return 0;   // 0 → CURLE_WRITE_ERROR，主动中断
    } else if (st->body) {
        st->body->append(ptr, n);
    }
    return n;
}

static int on_progress(void* ud, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* st = static_cast<CurlState*>(ud);
    return (st->cancel && st->cancel->cancelled()) ? 1 : 0;   // 非 0 → 中断
}
```

绑定：`WRITEFUNCTION=&on_write`、`WRITEDATA=&st`、`NOPROGRESS=0L`、`XFERINFOFUNCTION=&on_progress`、`XFERINFODATA=&st`。

### 7.3 SSE 解析九条细则

1. 行结束符 **CRLF 与 LF 都认**（只认「\n\n」在真实流量上一个事件都切不出来）
2. **空行 = 一个事件结束**
3. 冒号开头是**注释行**（心跳），跳过，不清空当前事件
4. `data:` 后允许 0 或 1 个空格，**只剥一个**（正文里的空格有意义）
5. 多行 `data:` 用**单个换行符拼接**
6. `event:` / `id:` 只在有效时取第一行
7. 末尾可能没有空行 → `finish()` 必须吐出残块
8. **逐字节喂入的结果必须与整体喂入完全一致**（唯一正确性标准）
9. 不认识的字段（`retry:` 等）忽略，不报错

### 7.4 取消语义

- `cancel()` 可能来自主线程或信号处理，**1 秒内**必须退出
- 路径：进度回调返回 1 → `curl_easy_perform` 返回 `CURLE_ABORTED_BY_CALLBACK` → `post_stream` 返回 `false` 且 `err.cancelled = true`
- **不要**用 `curl_easy_pause` 或直接 `curl_easy_cleanup` 打断
- SIGINT handler 里只允许置 `sig_atomic_t` 或调 `CancelToken::cancel()`，禁止 I/O

### 7.5 错误路径（W1 必须跑通，不能只跑 happy path）

| 场景 | 期望 |
| --- | --- |
| 错误的 key（401） | `http_status == 401`，`err.message` 是厂商返回原文 |
| 连不通（url 改 `127.0.0.1:1`） | `http_status == 0`，`err` 是 curl 描述 |
| 连接超时（`connect_timeout_ms=1`） | 1 秒内返回 `false`，不挂死 |
| 传输中途取消 | `cancelled == true`，进程 1 秒内退出 |
| HTTP 200 但流里出现 error 帧 | 也要返回 `false` 并填 `err` |

## 8. 测试

### 8.1 录制夹具（需要你的 DeepSeek key）

```bash
export DEEPSEEK_API_KEY=sk-你的key
curl -sN --http1.1 https://api.deepseek.com/chat/completions \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $DEEPSEEK_API_KEY" \
  -d '{"model":"deepseek-chat","stream":true,"messages":[{"role":"user","content":"用三句话解释 epoll"}]}' \
  > tests/fixtures/deepseek_stream.sse
```

`-N` 与 `--http1.1` 都不能省。录完检查：`head -3` 有 `data: {...}`、`tail -2` 有 `data: [DONE]`。**文件可以提交**（不含 key）。

### 8.2 四个用例（`tests/test_sse.cpp`，自定义 CHECK，不引入 gtest）

1. **整体喂入**：整读夹具 → 事件数 > 0、首条 data 能解析成 JSON、末条是 `[DONE]`
2. **逐字节重放**：每字节单独 `feed()` 一次，结果与用例 1 **逐字段相等**
3. **混合边界**：手写输入，含 CRLF、冒号注释、多行 `data:`、`data:` 后无空格 → 断言拼接正确、注释被跳过
4. **残块**：末尾无空行 → `finish()` 吐出最后一个事件；再调一次不重复吐

### 8.3 没有 key 时（回退方案）

真实夹具已在 2026-11-25 录制并重跑通过；手写夹具保留作无 key 回退。GLM / 千问 /
DeepSeek 三家真实 200 已在 2026-11-28 验证，原始输出见 §14.5 与 §15。

## 9. 验收 6 条（全部可执行）

```bash
# 1 零警告
cmake -S . -B build && cmake --build build -j 2>&1 | grep -c "warning:"   # 期望 0

# 2 测试全过
ctest --test-dir build --output-on-failure                                # 期望 100% passed

# 3 流式输出（人工看：是否逐字；CLI 默认 provider 是 glm）
GLM_API_KEY=<你的key> ./build/llmchat "你好"
DASHSCOPE_API_KEY=<你的key> ./build/llmchat --provider qwen "你好"
DEEPSEEK_API_KEY=<你的key> ./build/llmchat --provider deepseek "你好"

# 4 Ctrl-C 秒退（人工看：1 秒内回到 shell）

# 5 仓库里没有任何 key
git grep -nE 'sk-[A-Za-z0-9_-]{16,}' ; echo "exit=$?"                     # 期望无匹配

# 6 提交数
git log --oneline | wc -l                                                 # 期望 >= 4
```

> 第 5 条用正则而不是裸 `sk-`：本文件与计划里含 `sk-你的key` 这类占位串，裸搜会误报。
>
> 2026-11-28 实测：GLM / 千问 / DeepSeek 三家真实 HTTP 200；首 chunk 约 520–594 ms，
> Ctrl-C 2 / 6 / 15 ms 回 shell；六条全部通过。完整命令与原始输出见 §15。

## 10. 常见坑（踩过就不算坑）

1. 把成员函数直接传给 `CURLOPT_WRITEFUNCTION` → 编译不过（没有 `this`，签名不匹配）
2. 忘了 `NOPROGRESS, 0L` → 取消「看起来实现了但不生效」
3. SSE 只认换行换行 → 真实流量是 CRLF，静默无输出
4. 行尾残留 `\r` → JSON 解析在第 0 字节失败
5. 用 `std::getline` 读响应体 → 流式下会一直等，像卡死；必须用写回调
6. 把 `data: [DONE]` 当 JSON 解析 → 先判 `isDoneMark()`
7. `CURLOPT_TIMEOUT`（秒）当毫秒用 → 5 秒变 5000 秒
8. 录夹具漏 `-N` → 录出来是空的
9. 在 provider 里拼 URL → 端点改动会漏改，统一放 `endpoint()`
10. 中文不刷新：重定向到文件时 `std::cout` 全缓冲，流式用 `std::flush` 或 `std::cerr`

## 11. 现在不要做的事（越界清单）

- ❌ GPT / Gemini / Responses API（要梯子，没做过的不写进简历）
- ❌ SMTP 邮件验证码（校园网封 25/465，演示不了）
- ❌ Web 前端、注册登录页
- ❌ spdlog / fmt / gflags / jsoncpp（日志用 `std::cerr`，JSON 用 nlohmann）
- ❌ registry 注册表（W2 再说；W1 用 `--provider` 显式选择，注册表是空转）
- ❌ function calling / 多模态 / 图片 / 语音
- ❌ SQLite / OpenSSL（W3）
- ❌ Windows 构建 / 跨平台 ifdef
- ❌ 改桌面上那个 epoll 服务器项目

## 12. 实施计划与节奏

> 原 `W1-实施计划.md` 已合并到这里。逐日执行记录见 §14，最终证据见 §15。

### 12.1 基线与预算

| 项 | 状态 |
| --- | --- |
| 系统 / 编译器 | Ubuntu，C++17，g++ 15.2.0，CMake 4.2.3 |
| 依赖 | libcurl 8.18.0 / nlohmann-json 3.11.3 / libsqlite3 3.46.1（只链接） |
| 仓库 | https://github.com/lwj189/llm-sdk（main） |
| 时间 | D1 收尾 10 分钟；D2 约 120；D3 约 150（含录夹具）；D4 约 120；D5 约 90 |

### 12.2 五天拆分

| Day | 预算 | 主要拆分 | 收口 |
| --- | --- | --- | --- |
| D1 | 10 min | 目录骨架 + IR / 取消 / 错误头 + CMake 三目标 + 占位测试 | 0 警告；`ctest` 1/1；`bdee496` / `1fbcd64` / `8603175` |
| D2 | 120 min | `CurlState` + 两个 C 回调（25）→ `perform()` 六项必设（30）→ `post()`（15）→ `post_stream()` + 取消（20）→ 临时 smoke 驱动（30） | 200 / stream / cancel / 401 / fail / timeout 六条；commit `feat: libcurl HTTP 客户端…` |
| D3 | 150 min | 先录真实夹具（约 30）→ 行扫描 + 半行/半事件挂起 → 四用例 → 逐字节重放与边界 | 四例全 PASS；真实夹具 32494 字节；commit `feat: SSE 解析器…` |
| D4 | 120 min | 构造函数 / endpoint / apiKey / isAvailable → buildBody → chat → chatStream 主流程 → 三家默认值 → CLI + SIGINT | 本地 mock 四条 + 真实 401；后补三家真实 200；commit `feat: OpenAI 兼容 provider…`，扩展 `f48546f` |
| D5 | 90 min | 警告复查 → README → 六条验收 → 四问自测 → 文档收口 | 0 警告、1/1、三家 200、无 key；commit `docs: README / NOTES 收口…` |

### 12.3 固定节奏

每 25–40 分钟一段，**每段结束代码必须能编译**。当天收尾四步：

1. `cmake --build build -j 2>&1 | grep -c "warning:"` → 必须是 `0`。
2. 跑当天验收命令，把**原始输出**贴出来（不许只说「已完成」）。
3. `git add -A` → 确认 `git status --short` 里没有 `build/`、没有 `.env` → commit。
4. 在 §14 补三行（做了什么 / 卡在哪 / 明天第一件事）。

### 12.4 风险与对策

| 风险 | 触发点 | 对策 |
| --- | --- | --- |
| 没有 DeepSeek key | D3 录夹具、D4 逐字验收 | 按 D-09：先用手写夹具跑通 4 个用例，真实夹具到位后重跑；**不许拿手写夹具冒充真实夹具** |
| 一天只有 1.5 小时 | 任何一天 | 每段可独立提交；当天没做完就停在一个可编译的分段边界，不跨天混写 |
| 取消"看起来实现了但不生效" | D2 的 `perform()` 段 | `NOPROGRESS=0` 打勾检查；D2 就写超时用例 |
| SSE 静默无输出 | D3 | 先用夹具跑通再加字节重放；只认 LF 会在真实流量上零事件 |
| 说明书没写清就想"顺手改" | D4 的 `deepseek.hpp` 段 | 先问再做；改动登记进 §13 |
| 一次写 300 行 | 每天 | 每段一个可编译检查点；`git diff --stat` 超 150 行就拆 |

---

### 12.5 已拍板问题（2026-11-22）

| # | 问题 | 结论 |
| --- | --- | --- |
| Q1 | `DeepSeekProvider` 声明放哪 | ✅ 批准新增 `include/llm/deepseek.hpp`（5 行，D4 阶段写），公开签名不改 |
| Q2 | 项目目录名 | ✅ 保持 `~/桌面/sdk`，不改名（CMake 工程名仍是 `llm_sdk`） |
| Q3 | `git init` 谁做 | ✅ 你自己做；命令与检查项见 §12.2 D1 |
| Q4 | DeepSeek key | ⏳ 机器上现在**没有** key（环境变量 / shell rc / `.env` 都查过）；D3 前需要你给，否则按 D-09 降级 |

---

## 13. 关键决定（D-01–D-15）

> 这些是执行过程中拍板、且会影响接口或验收方式的决定；不同意可以改，但改之前先看「理由」。

| # | 决定 | 理由 |
| --- | --- | --- |
| D-01 | D2 的临时驱动写成 `cli_chat.cpp` 的 `--http-smoke*` 子命令，不新增 target | 附录 CMakeLists 不动；D4 直接删掉这几个分支 |
| D-02 | `parseChunk` 返回 `true` = 成功；error 帧填 `err` 并返回 `false` | 说明书只给了签名，语义按"false 即出错"统一 |
| D-03 | `finish()` 幂等（第二次返回空） | 用例 4 明确要求 |
| D-04 | `on_chunk` 返回 false → `chatStream` 返回 false 且 `err.cancelled = true` | 与"未正常完成"的语义一致 |
| D-05 | `SSEParser` 增加私有成员（pending 字段），公开签名不变 | 只靠 `buf_` 存不下"半行 + 半事件" |
| D-06 | `provider.hpp` 加 `#include "llm/sse.hpp"` | 钩子签名里用了 `SSEEvent`，不 include 编译不过 |
| D-07 | 401 用例用 `Authorization: Bearer invalid` | 仓库里不放任何像真实 key 的字符串；验收第 5 条会扫 |
| D-08 | 错误文本 = `curl_easy_strerror(code)` + `" (CURLcode N)"` | 401 时厂商原文由 `parseErrorBody` 负责，两者不混 |
| D-09 | 没有 key 时 D3 不硬停：先用手写夹具把解析器与 4 个用例跑通，真实夹具到位后重跑，**只有重跑通过才算 D3 完成** | 手写夹具能验逐字节/CRLF/注释/多行 data，验不了"真实流量长什么样"；两件事分开记，不混为一谈 |
| D-10 | 验收第 5 条用 `git grep -nE 'sk-[A-Za-z0-9_-]{16,}'`（无匹配才算过） | 文档随库提交，说明书自带 `sk-你的key` 占位串；真实 key ≥ 16 位，正则只抓真 key |
| D-11 | 文档随库一起提交（现已合并为单文件） | 文档跟代码同仓，换机器/下个会话都不会丢 |
| D-12 | SSE 测试用自定义 `CHECK` / `CHECK_STR` / `CHECK_SIZE`，不用 `assert` | Release 的 `NDEBUG` 会把 `assert` 编掉，`ctest` 会假过；自定义宏失败打印用例号/期望/实际并返回 1 |
| D-13 | D4 的 401 验收改成 `DEEPSEEK_API_KEY=invalid ./build/llmchat "hi"` | D-01 要求 D4 删除 `--http-smoke*`；走正式 CLI 才能同时验 `parseErrorBody` 与 CLI 错误输出 |
| D-14 | 2026-11-28 扩展新增 `GlmProvider` / `QwenProvider`；CLI 默认 `glm`，`--provider` 可切 `deepseek` / `qwen`；GLM 覆盖 `usageOptionKey()` 为 `chunk_include_usage` | 用户要求兼容 GLM 与千问；三家请求/响应同形，只差 base_url、key 变量、默认模型与流式 usage 字段名；保留 DeepSeek 不破坏已有验收 |
| D-15 | 适配验收采用三类证据：三家真实 200、GLM/Qwen 请求体 mock、SSE 四用例；不拿单一截图当证明 | 真实 200 证明端到端，mock 证明字段差异，单测证明解析边界；演示时可复现 |

---

## 14. 开发日志（按天）

> 原 `NOTES.md`。保留每天的「做了什么 / 卡在哪 / 明天第一件事」和当天原始输出。
> 历史记录里提到的 `sdk-notes/` 文件已在本次整合中删除，对应内容分别在 §1–§13、§15。

### 14.1 五天概览

| Day | 主题 | 结果 |
| --- | --- | --- |
| D1 | 目录骨架 + IR / 取消 / 错误类型 | clean build 0 警告；`ctest` 1/1；仓库已推送 |
| D2 | libcurl HTTP 客户端（`post` / `post_stream`） | 200 / stream / cancel / 401 / fail / timeout 六条全 PASS |
| D3 | SSE 解析器 + 逐字节重放测试 | 真实夹具 32494 字节；四用例 ALL PASS；Release 同样通过 |
| D4 | Provider + 流式 CLI + GLM / 千问扩展 | 三家真实 HTTP 200；逐字输出、usage、Ctrl-C 全部通过 |
| D5 | 文档收口 + 六条验收 | 0 警告、`ctest` 1/1、三家 200、无 key；验收时 16 个提交 |

---

### 14.2 D1：骨架与 IR（2026-11-22）

- **做了什么**：目录骨架 + `ir.hpp` / `cancel.hpp` / `error.hpp` + 附录原样的 CMakeLists + 6 个占位翻译单元；在真实目录 `~/桌面/sdk` 重新 configure/build，零警告，`ctest` 1/1（占位 main，不含断言）。说明书与计划放在 `sdk-notes/`（后于 2026-11-28 合并进本文），随仓库提交。
- **卡在哪**：沙箱已放开，骨架已经落盘；`nlohmann-json3-dev`（3.11.3）与 `libsqlite3-dev`（3.46.1）已装好。git 仓库已建并推到 GitHub（`bdee496` 首提交、`1fbcd64` 文档整改、`8603175` D1 收尾）。
- **明天第一件事**：D2 —— `HttpClient::post` 打通，跑通 200 / 401 / 连接失败三条路径。

#### D1 验收（2026-11-23 复跑）

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   → exit=0
cmake --build build -j                          → exit=0，warning: 0，error: 0
ctest --test-dir build --output-on-failure      → 100% tests passed, 1/1
./build/llmchat                                 → "D1 骨架，功能在 D4 实现"，exit=0
git grep -nE 'sk-[A-Za-z0-9_-]{16,}'            → 0 命中
git log --oneline                               → bdee496、1fbcd64、8603175（origin/main 已同步）
```

### 14.3 D2：HTTP 层（2026-11-24）

- **做了什么**：`src/http_client.cpp` 写满——`CurlState` + `on_write` / `on_progress` 两个 C 回调、全项目唯一一份 `perform()`（六项必设都在这一段）、`post()` 把响应体收进 `body_out`、`post_stream()` 逐块回调 + 取消；`examples/cli_chat.cpp` 加 6 个 `--http-smoke*` 临时驱动（D4 已按 D-01 删除）。clean build 0 警告，`ctest` 1/1。
- **卡在哪**：机器上仍然没有 `DEEPSEEK_API_KEY`（Q4 / D-09），所以「200」用本地 python 假服务 + 假 key 验（`LLM_SMOKE_URL` 覆盖地址）；401 / fail / timeout 打真实 `api.deepseek.com`。真实 key 的 200 到 D3 拿到 key 后补跑。
- **明天第一件事**：D3 —— 有 key 先录 `tests/fixtures/deepseek_stream.sse`；没 key 就按 D-09 先写解析器 + 手写夹具，把 4 个用例跑通。

#### D2 验收（2026-11-24，clean build）

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

### 14.4 D3：SSE 解析器（2026-11-25）

- **做了什么**：`include/llm/sse.hpp` + `src/sse.cpp` 实现 SSE 解析器——`feed` 行扫描、半行/半事件挂起、跨块 CRLF、`:` 注释、多行 `data`、`finish` 幂等；`tests/test_sse.cpp` 四个用例（整读 / 逐字节重放 / 混合边界 / 残行 finish）；新增手写夹具 `tests/fixtures/handwritten_stream.sse`；按说明书 §8.1 用真实 key 录 `tests/fixtures/deepseek_stream.sse`（32494 字节 / 236 行 / 118 条 `data` / 117 条 JSON chunk + `[DONE]`）。clean build 0 警告，`ctest` 1/1，真实夹具四例全 PASS；Release（`NDEBUG`）构建同样 0 警告、`ctest` 1/1、四例 PASS。
- **卡在哪**：原卡点是机器上没有 `DEEPSEEK_API_KEY`，真实夹具无法录；现已录到并重跑通过，D3 没有遗留问题。
- **明天第一件事**：D4 —— 先建 `include/llm/deepseek.hpp`，再填 `src/providers/openai_compat.cpp` 的 `buildBody` / `chat` / `chatStream`，最后接流式 CLI（计划 §5）。

> 真实夹具观察：响应里的 `model` 是 `deepseek-flash`（请求写的是 `deepseek-chat`）；不要拿响应 model 做一致性校验。

#### D3 验收（2026-11-25，clean build）

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

### 14.5 D4：Provider + 流式 CLI（2026-11-26 起，2026-11-28 收口）

- **做了什么**：新增 `include/llm/provider.hpp`（`LLMProvider` + 模板方法式 `OpenAICompatProvider`）与 `include/llm/deepseek.hpp`；填满 `src/providers/openai_compat.cpp` 的 `endpoint` / `apiKey` / `isAvailable` / `buildBody` / `chat` / `chatStream` / `parseChunk` / `parseErrorBody`；`src/providers/deepseek.cpp` 填 DeepSeek 默认值；`examples/cli_chat.cpp` 改成真正的流式 CLI（逐字打印、reasoning 打到 stderr、usage、SIGINT → `CancelToken`、退出码 130），按 D-01 删除全部 `--http-smoke*`。11-28 又按用户要求扩展 GLM / 千问：新增 `glm.hpp` / `qwen.hpp` 与对应 `.cpp`，`usageOptionKey()` 钩子默认 `include_usage`、GLM 覆盖为 `chunk_include_usage`；CLI 支持 `--provider glm|qwen|deepseek` 与 `--model`，默认 `glm`，保留 DeepSeek。
- **卡在哪**：11-26 DeepSeek 旧 key 真实请求 401，真实 200 未验；本地 mock 的流式 / 非流式 / 401 / 取消四条全 PASS。11-28 GLM key 也返回 401（标准端点与 coding 端点、多个模型都试过），千问没有 key；之后拿到有效 key，三家真实 200 才补齐。
- **明天第一件事**：D5 收口；补 D4 真实 200 与 Ctrl-C，按说明书 §9 跑六条验收 + 四问自测，最后 `docs:` commit。

#### D4-A：本地 mock 与 401（2026-11-26）

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

#### D4-B：GLM / 千问请求体（2026-11-28，本地 mock）

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

#### D4-C：三家真实 200（2026-11-28 收口）

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

### 14.6 D5：收口（2026-11-28）

- **做了什么**：警告复查；README 补编译 / 用法 / 目录 / 设计三句话；NOTES 补 D4 收口与 D5；说明书 §9 六条验收与四问自测；计划看板与现状文档同步；新增 §15 汇总真实 200、流式、取消和适配差异证据。
- **卡在哪**：无。
- **明天第一件事**：无，W1 收口；准备现场演示（见 §15.7）。

#### D5 验收（六条）

```text
1. 零警告：configure_rc=0，build_rc=0，warnings=0，errors=0
2. 测试：100% tests passed, 0 tests failed out of 1
   ./build/llm_tests：fixture recorded，[case 1]~[case 4] ALL PASS
3. 流式：GLM / Qwen / DeepSeek 三家真实 200；单次实测首 chunk 594 / 568 / 520 ms，
   chunk 数 78 / 38 / 94，usage 正常（详见 §15）
4. Ctrl-C：SIGINT 后 2 / 6 / 15 ms 退出，rc=130
5. key 扫描：扩展正则无匹配，scan_exit=1
6. 提交数：验收时 15（≥4）；docs 收口提交后 16（本次整理提交另计）
```

### 14.7 关键观察与坑

1. **HTTP 4xx/5xx 不是传输失败**：curl 仍返回 `CURLE_OK`，`ok=true`，状态码要单独取；错误体交给 `parseErrorBody`。
2. **响应里的 model 可能和请求不同**：DeepSeek 请求 `deepseek-chat`，响应写 `deepseek-flash`；不要拿响应 model 做一致性校验。
3. **SSE 边界比正常流更重要**：半行、半事件、跨块 CRLF、`:` 注释、多行 `data`、末尾无空行，都要有测试；逐字节重放是发现边界 bug 的最快方式。
4. **测试不要用 `assert`**：Release 的 `NDEBUG` 会把断言编掉，`ctest` 会假过；D3 改用自定义 `CHECK`，失败打印用例号 / expected / actual 并返回 1。
5. **GLM 的流式 usage 字段不同**：GLM 用 `stream_options.chunk_include_usage`，Qwen / DeepSeek 用 `include_usage`；差异放在 `usageOptionKey()` 一个钩子里，主流程不动。
6. **key 只进环境变量**：不写仓库、文档、夹具或提交；聊天里出现过的 key 建议演示后轮换。首 chunk / 总耗时都是单次实测，不能当 p50 / p95。

## 15. 验收报告（2026-11-28）

> 结论：W1 已完成。DeepSeek / GLM / 千问三家都走同一套 `LLMProvider` 接口，
> 真实 API 均验到 HTTP 200，逐字流式、usage、Ctrl-C 取消、SSE 四用例与六条验收全部通过。
> 本文所有 key 都只放在进程环境变量里，仓库中没有 key。

### 15.1 环境与版本

- 系统：Ubuntu，C++17，CMake
- 依赖：libcurl、nlohmann-json、libsqlite3（仅链接）
- 验收时代码提交：`f48546f`（GLM / 千问 provider 与 CLI 选择）
- 默认 provider：`glm`；可用 `--provider qwen|deepseek` 切换

### 15.2 六条验收

| # | 验收项 | 命令 | 结果 |
| --- | --- | --- | --- |
| 1 | 零警告 | `cmake -S . -B build && cmake --build build -j` | 0 warning / 0 error |
| 2 | 测试全过 | `ctest --test-dir build --output-on-failure` | 100% tests passed, 1/1 |
| 3 | 三家流式 | 见 §15.3 | 三家 HTTP 200，逐字输出 |
| 4 | Ctrl-C 秒退 | 见 §15.4 | 2 / 6 / 15 ms，rc=130 |
| 5 | 仓库无 key | `git grep -nE 'sk-[A-Za-z0-9_.-]{16,}|[0-9a-f]{32}\.[A-Za-z0-9]{16}'` | 无匹配 |
| 6 | 提交数 | `git log --oneline \| wc -l` | 验收时 15，收口后 16，≥4 |

补充：SSE 真实夹具 `tests/fixtures/deepseek_stream.sse`（32494 字节 / 118 条 data）
上的 4 个用例全部 PASS：整体喂入、逐字节重放、CRLF/注释/多行 data、finish 残块。

### 15.3 三家真实 200 与流式

同一段提示词：`用三句话解释 epoll`。下表是单次实测，网络有波动，不作为性能基准；
关键是三家都返回 200、都有多个流式 chunk、usage 正常。

| provider | 模型 | HTTP | 首 chunk | chunk 数 | 总耗时 | usage（prompt/completion/total） |
| --- | --- | --- | --- | --- | --- | --- |
| glm | glm-4-flash | 200 | 594 ms | 78 | 5686 ms | 10 / 94 / 104 |
| qwen | qwen-plus | 200 | 568 ms | 38 | 4025 ms | 13 / 156 / 169 |
| deepseek | deepseek-chat | 200 | 520 ms | 94 | 1319 ms | 10 / 120 / 130 |

显式 HTTP 状态验证（非流式，只回复 OK）：

```text
glm:      http=200 model=glm-4-flash   content='OK'
qwen:     http=200 model=qwen-plus     content='OK'
deepseek: http=200 model=deepseek-flash content='OK'
```

流式输出摘要（实际终端会逐字出现）：

```text
[glm]      epoll 是一种高效的 I/O 多路复用机制，用于 Linux 系统中，它允许单个进程
           同时监控多个文件描述符的状态变化……
[qwen]     epoll 是 Linux 内核提供的一种高效 I/O 事件通知机制，用于替代传统的
           select 和 poll……
[deepseek] 1. epoll 是 Linux 下的一种 I/O 多路复用机制，让单个线程能同时监视大量
           文件描述符上的读写事件……
```

每家的 stderr 都有：

```text
[provider] glm|qwen|deepseek
[usage] prompt=... completion=... total=...
[done] ... ms
```

### 15.4 Ctrl-C 取消

用长提示词启动流式请求，0.7 秒后发送 SIGINT，测量从信号到进程退出：

| provider | SIGINT 后退出 | 退出码 | 取消前已输出 |
| --- | --- | --- | --- |
| glm | 2 ms | 130 | 8 字符 |
| qwen | 6 ms | 130 | 0 字符 |
| deepseek | 15 ms | 130 | 69 字符 |

三条都远低于「1 秒内回到 shell」的验收线，说明 `CancelToken` → curl 进度回调这条链
在真实网络上生效，不是只在 mock 里有效。

### 15.5 适配差异的证明

三家共用同一套主流程：

```text
LLMProvider（纯虚接口）
  └── OpenAICompatProvider（chatStream final，6 个 protected 钩子）
        ├── DeepSeekProvider
        ├── GlmProvider    （usageOptionKey = chunk_include_usage）
        └── QwenProvider   （usageOptionKey = include_usage）
```

差异只在默认值和一个小钩子：

| provider | base_url | 默认模型 | 流式 usage 字段 |
| --- | --- | --- | --- |
| glm | open.bigmodel.cn/api/paas/v4 | glm-4-flash | `stream_options.chunk_include_usage` |
| qwen | dashscope.aliyuncs.com/compatible-mode/v1 | qwen-plus | `stream_options.include_usage` |
| deepseek | api.deepseek.com | deepseek-chat | `stream_options.include_usage` |

本地 mock 抓到的请求体（证明字段没有写错）：

```text
glm  model=glm-4-flash stream_options={"chunk_include_usage": true}
qwen model=qwen-plus   stream_options={"include_usage": true}
```

真实 200 证明 base_url、key、模型名、请求体都能被服务端接受；SSE 四用例证明解析器
对真实 DeepSeek 夹具和多种边界都正确。两者合起来才是「适配没问题」，不是只看一张截图。

### 15.6 四问自测

1. **`LLMProvider` 析构为什么必须 virtual？**
   因为上层用 `std::unique_ptr<LLMProvider>` 持有派生类；通过基类指针 delete 时，
   如果析构不是 virtual，派生类析构不会执行，`std::string` / `std::unique_ptr`
   成员泄漏，行为未定义。所以 `virtual ~LLMProvider() = default;`。
2. **`chatStream` 拿到一块数据后干什么？为什么用 `std::function`？**
   HTTP 写回调把字节交给 sink；sink 交给 `SSEParser::feed`，解析出 `SSEEvent`；
   provider 再解析 JSON，取 `choices[0].delta.content`，通过 `std::function`
   回调给 CLI。用 `std::function` 是为了把「怎么输出」从 provider 里解耦：
   打印、收集、取消都可以换，不需要为每种输出再加虚函数。
3. **为什么主流程 `final`、只留 6 个钩子？**
   请求组装、curl 调用、状态码判断、SSE 循环、`[DONE]` 早停、错误体解析这些
   容易写错的部分只写一次；派生类只填 endpoint/key/model/usage 字段等默认值。
   模板方法避免三家复制主流程，也避免某家改坏取消或错误处理。
4. **加一个完全不兼容 OpenAI 的厂商，改哪里？**
   新增一个直接实现 `LLMProvider` 的类（`chat` / `chatStream` / `name`），
   加头文件、cpp、CMake，再在 CLI 的 provider 选择处注册。核心的 `LLMProvider`
   接口、CLI 流式打印、取消链路不用动；`OpenAICompatProvider` 也不用改。
   这就是「加一个新类，主流程一行不动」的含义。

### 15.7 现场演示怎么讲

**3 分钟版：**

```bash
# 1. 干净状态
git status -sb
git log --oneline -5

# 2. 编译与单测
cmake --build build -j
ctest --test-dir build --output-on-failure

# 3. 同一段提示词，三家真实流式（key 提前放进环境变量，不要贴屏幕）
./build/llmchat --provider glm "用三句话解释 epoll"
./build/llmchat --provider qwen "用三句话解释 epoll"
./build/llmchat --provider deepseek "用三句话解释 epoll"
```

**展示要点：**

1. **先给结论**：W1 完成，三家真实 200，流式、usage、取消、六条验收都过。
2. **再给证据**：`ctest` 1/1；三家终端输出；`git grep` 无 key；`git log` 有提交。
3. **最后讲设计**：IR 隔离、模板方法、取消链路；用 §15.6 四问回答追问。
4. 如果对方只看文档：把本文 §14 和 §15 的原始输出给他；如果现场看，直接跑上面三条命令。
5. 不要用截图代替命令；截图只能证明当时，命令可以复现。

### 15.8 安全说明

- 三家 key 只在验收进程的环境变量中使用，没有写进仓库、文档、夹具或提交。
- 验收后 `git grep` 扩展正则无匹配；`git status` 干净。
- 这些 key 已经在聊天/终端里出现过，演示结束后建议到各控制台轮换。