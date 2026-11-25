// D3：SSE 解析器。只做一件事：把任意切分的字节流切成 SSEEvent。
// 它不认识 JSON，也不认识 [DONE] —— 这两件事属于 provider（说明书 §7.3 红线）。
//
// 核心状态只有三块：
//   buf_      上一块没拼完的半行（网络可能在任意字节处切断）
//   cur_      正在拼的事件（event / id / data）
//   has_*_    字段是否出现过 → 空行到来时据此决定「发射」还是「跳过」
//
// 逐字节重放与整体喂入结果必须完全一致，这是唯一正确性标准（计划 §4.3 用例 2）。

#include "llm/sse.hpp"

#include <utility>

namespace llm {

namespace {

// 行结束符认 CRLF 与 LF 两种；这里只剥掉行尾那一个 \r。
// 跨块 CRLF 不需要特殊分支：\r 留在 buf_ 里等下一块的 \n，凑齐后才成为一个完整行。
void strip_cr(std::string& line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
}

} // namespace

void SSEParser::clear_event() {
    cur_ = SSEEvent{};
    has_event_ = false;
    has_id_ = false;
    has_data_ = false;
    has_fields_ = false;
}

void SSEParser::emit_event(std::vector<SSEEvent>& out) {
    if (!has_fields_) return;

    // 多行 data 在拼接时每行都补了一个 \n；发射前去掉最后那一个，
    // 内部换行保留（说明书 §7.3 第 5 条：多行 data 用单个换行符拼接，末尾不带换行）。
    // has_data_ 为 true 时 cur_.data 必然以 '\n' 结尾（每行至少补一个）。
    if (has_data_) cur_.data.pop_back();

    out.push_back(std::move(cur_));
    clear_event();
}

void SSEParser::consume_line(std::string line, std::vector<SSEEvent>& out) {
    strip_cr(line);

    // 空行 = 一个事件结束；没有字段的空行（比如连续空行）直接跳过。
    if (line.empty()) {
        emit_event(out);
        return;
    }

    // 冒号开头是注释（心跳）。跳过，但绝不清空当前事件 —— 注释可能夹在
    // 同一个事件的几行 data 之间（说明书 §7.3 第 3 条）。
    if (line.front() == ':') return;

    const std::size_t colon = line.find(':');
    const std::string field = (colon == std::string::npos) ? line : line.substr(0, colon);
    std::string value = (colon == std::string::npos) ? std::string() : line.substr(colon + 1);

    // 只剥一个前导空格：正文里的空格有意义（说明书 §7.3 第 4 条）。
    if (!value.empty() && value.front() == ' ') value.erase(value.begin());

    if (field == "data") {
        cur_.data.append(value);
        cur_.data.push_back('\n');
        has_data_ = true;
    } else if (field == "event") {
        if (!has_event_) {  // 只在第一次出现时取值
            cur_.event = std::move(value);
            has_event_ = true;
        }
    } else if (field == "id") {
        if (!has_id_) {  // 只在第一次出现时取值
            cur_.id = std::move(value);
            has_id_ = true;
        }
    } else {
        // retry: 以及将来不认识的字段一律忽略，不报错（说明书 §7.3 第 9 条）。
        return;
    }

    has_fields_ = true;
}

std::vector<SSEEvent> SSEParser::feed(const char* p, std::size_t n) {
    std::vector<SSEEvent> out;
    if (p == nullptr || n == 0) return out;

    buf_.append(p, n);

    // 只消费完整的行；最后一段没有 \n 的字节留在 buf_ 里等下一块。
    std::size_t start = 0;
    for (;;) {
        const std::size_t nl = buf_.find('\n', start);
        if (nl == std::string::npos) break;
        consume_line(buf_.substr(start, nl - start), out);
        start = nl + 1;
    }
    if (start != 0) buf_.erase(0, start);

    return out;
}

std::vector<SSEEvent> SSEParser::finish() {
    std::vector<SSEEvent> out;

    // 末尾可能没有空行：把残留缓冲当成最后一行处理。
    if (!buf_.empty()) {
        std::string line;
        line.swap(buf_);
        consume_line(std::move(line), out);
    }

    // 如果上一行是完整的 data 但后面没有空行，事件还挂在 cur_ 里。
    emit_event(out);
    return out;
}

void SSEParser::reset() {
    buf_.clear();
    clear_event();
}

} // namespace llm
