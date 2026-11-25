// D3：SSEParser 的四个用例（说明书 §8.2 / 计划 §4.3）。
// 不引入 gtest；失败时打印行号、用例号、期望与实际，然后让 main 返回 1。
//
// 夹具选择：优先读 fixtures/deepseek_stream.sse（真实录制）；
// 不存在时退回 fixtures/handwritten_stream.sse（按 D-09 的手写夹具）。
// 真实夹具到位后不需要改代码，直接重跑即可 —— 测试会打印实际用的是哪一个。

#include "llm/sse.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using llm::SSEEvent;
using llm::SSEParser;

namespace {

int g_failures = 0;

// 不用 assert：Release 构建会定义 NDEBUG 把 assert 编掉，测试就变成永远通过。
// 这里用显式的 check，任何构建类型下失败都会打印并让进程返回 1。
bool check(bool ok, const char* expr, int line) {
    if (ok) return true;
    std::cerr << "FAIL line " << line << ": " << expr << "\n";
    ++g_failures;
    return false;
}

std::string visible(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out += c;
        }
    }
    return out;
}

bool check_str_eq(const std::string& actual, const std::string& expected,
                  const std::string& what, int line) {
    if (actual == expected) return true;
    std::cerr << "FAIL line " << line << " [" << what << "]\n"
              << "  expected: \"" << visible(expected) << "\"\n"
              << "  actual:   \"" << visible(actual) << "\"\n";
    ++g_failures;
    return false;
}

bool check_size_eq(std::size_t actual, std::size_t expected,
                   const std::string& what, int line) {
    if (actual == expected) return true;
    std::cerr << "FAIL line " << line << " [" << what << "]\n"
              << "  expected: " << expected << "\n"
              << "  actual:   " << actual << "\n";
    ++g_failures;
    return false;
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_STR(actual, expected, what) check_str_eq((actual), (expected), (what), __LINE__)
#define CHECK_SIZE(actual, expected, what) check_size_eq((actual), (expected), (what), __LINE__)

std::string source_dir() {
    const std::string file = __FILE__;
    const std::size_t slash = file.find_last_of("/\\");
    return (slash == std::string::npos) ? std::string(".") : file.substr(0, slash);
}

bool file_exists(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return in.good();
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "FAIL cannot open fixture: " << path << "\n";
        ++g_failures;
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct Fixture {
    std::string path;
    std::string text;
    bool handwritten = false;
};

Fixture load_fixture() {
    const std::string dir = source_dir();
    const std::string real = dir + "/fixtures/deepseek_stream.sse";
    const std::string hand = dir + "/fixtures/handwritten_stream.sse";
    if (file_exists(real)) return {real, read_file(real), false};
    return {hand, read_file(hand), true};
}

void append_events(std::vector<SSEEvent>& dst, std::vector<SSEEvent> src) {
    for (auto& ev : src) dst.push_back(std::move(ev));
}

// 用例 1：整读夹具，一次 feed。事件数 > 0；首条 data 能喂进 nlohmann；
// 最后一条 data 是 [DONE]。
void test_fixture_whole(const Fixture& fx) {
    std::cout << "[case 1] whole fixture\n";

    SSEParser parser;
    std::vector<SSEEvent> events = parser.feed(fx.text.data(), fx.text.size());
    append_events(events, parser.finish());

    CHECK(!events.empty());
    if (events.empty()) return;

    CHECK(!events.front().data.empty());

    const nlohmann::json first = nlohmann::json::parse(events.front().data, nullptr, false);
    CHECK(!first.is_discarded());
    CHECK(first.contains("choices"));

    CHECK_STR(events.back().data, "[DONE]", "case1 last event data");

    // 除了 [DONE]，每条 data 都应是完整 JSON；[DONE] 必须恰好一次且在最后。
    // 这样能抓住「中间漏掉事件」这类只看首尾抓不到的错误。
    std::size_t done_count = 0;
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (events[i].data == "[DONE]") {
            ++done_count;
            CHECK(i + 1 == events.size());
        } else {
            const nlohmann::json chunk = nlohmann::json::parse(events[i].data, nullptr, false);
            CHECK(!chunk.is_discarded());
        }
    }
    CHECK_SIZE(done_count, 1, "case1 [DONE] count");

    // 手写夹具已知 6 条事件；真实夹具不写死条数。
    if (fx.handwritten) CHECK_SIZE(events.size(), 6, "case1 handwritten event count");
}

// 用例 2：每个字节单独 feed 一次，事件序列与用例 1 逐字段完全相等。
// 这一条是解析器的唯一正确性标准：网络在任意位置切断都不能改变结果。
void test_byte_replay(const Fixture& fx) {
    std::cout << "[case 2] byte-by-byte replay\n";

    SSEParser whole_parser;
    std::vector<SSEEvent> whole = whole_parser.feed(fx.text.data(), fx.text.size());
    append_events(whole, whole_parser.finish());

    SSEParser byte_parser;
    std::vector<SSEEvent> bytes;
    for (std::size_t i = 0; i < fx.text.size(); ++i) {
        append_events(bytes, byte_parser.feed(fx.text.data() + i, 1));
    }
    append_events(bytes, byte_parser.finish());

    CHECK_SIZE(bytes.size(), whole.size(), "case2 event count");

    const std::size_t n = std::min(bytes.size(), whole.size());
    for (std::size_t i = 0; i < n; ++i) {
        const std::string tag = "case2 event[" + std::to_string(i) + "]";
        CHECK_STR(bytes[i].event, whole[i].event, tag + ".event");
        CHECK_STR(bytes[i].id, whole[i].id, tag + ".id");
        CHECK_STR(bytes[i].data, whole[i].data, tag + ".data");
    }
}

// 用例 3：手写混合边界 —— CRLF、冒号注释、多行 data、data: 后无空格。
// 注释夹在两行 data 之间时，不能清空当前事件。
void test_mixed_boundaries() {
    std::cout << "[case 3] mixed boundaries\n";

    const std::string input =
        ": keep-alive\r\n"
        "event: message\r\n"
        "id: 42\r\n"
        "data: {\"a\":1}\r\n"
        ": heartbeat between data lines\r\n"
        "data:第二行\r\n"
        "\r\n"
        "data: tail\r\n"
        "\r\n";

    SSEParser parser;
    const std::vector<SSEEvent> events = parser.feed(input.data(), input.size());

    CHECK_SIZE(events.size(), 2, "case3 event count");
    if (events.size() >= 1) {
        CHECK_STR(events[0].event, "message", "case3 e0.event");
        CHECK_STR(events[0].id, "42", "case3 e0.id");
        CHECK_STR(events[0].data, "{\"a\":1}\n第二行", "case3 e0.data");
    }
    if (events.size() >= 2) {
        CHECK_STR(events[1].event, "", "case3 e1.event");
        CHECK_STR(events[1].id, "", "case3 e1.id");
        CHECK_STR(events[1].data, "tail", "case3 e1.data");
    }
}

// 用例 4：末尾没有空行 → finish() 吐出最后一个事件；再调一次必须返回空。
void test_finish_residual() {
    std::cout << "[case 4] finish residual\n";

    {
        SSEParser parser;
        const std::string input = "data: 最后一条";
        CHECK(parser.feed(input.data(), input.size()).empty());

        const std::vector<SSEEvent> last = parser.finish();
        CHECK_SIZE(last.size(), 1, "case4 finish event count");
        if (last.size() == 1) CHECK_STR(last[0].data, "最后一条", "case4 data");

        CHECK(parser.finish().empty());
    }

    {
        // 残留缓冲本身还有一行没结尾：finish() 必须先把它当成最后一行解析。
        SSEParser parser;
        const std::string input = "data: a\ndata: b";
        CHECK(parser.feed(input.data(), input.size()).empty());

        const std::vector<SSEEvent> last = parser.finish();
        CHECK_SIZE(last.size(), 1, "case4 multi-line finish count");
        if (last.size() == 1) CHECK_STR(last[0].data, "a\nb", "case4 multi-line data");

        CHECK(parser.finish().empty());
    }
}

} // namespace

int main() {
    std::cout << "=== D3 SSE parser tests ===\n";

    const Fixture fixture = load_fixture();
    std::cout << "fixture: " << fixture.path
              << (fixture.handwritten ? "  (handwritten; real fixture pending key)"
                                      : "  (recorded)")
              << "\n";

    test_fixture_whole(fixture);
    test_byte_replay(fixture);
    test_mixed_boundaries();
    test_finish_residual();

    if (g_failures != 0) {
        std::cerr << "\n" << g_failures << " check(s) FAILED\n";
        return 1;
    }
    std::cout << "\nALL PASS\n";
    return 0;
}
