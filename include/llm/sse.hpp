#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace llm {

// 一个 SSE 事件。解析器只负责「把字节流切成事件」：
// 不认识 JSON，也不认识 [DONE] —— 那是 provider 的事（说明书 §7.3 红线）。
struct SSEEvent {
    std::string event;  // event: 字段，缺省为空
    std::string id;     // id: 字段
    std::string data;   // 多行 data 用单个 \n 拼接，末尾不带换行
};

class SSEParser {
public:
    // 喂入任意长度的原始字节（可能在任意位置切断），返回本次能完整解析出来的事件
    std::vector<SSEEvent> feed(const char* p, std::size_t n);
    // 流结束时调用：把缓冲区里的残留解析成一个事件（如果语义完整）
    std::vector<SSEEvent> finish();
    void reset();

private:
    // 公开签名不变，私有成员允许增加（说明书 §6.2）
    void consume_line(std::string line, std::vector<SSEEvent>& out);
    void emit_event(std::vector<SSEEvent>& out);
    void clear_event();

    std::string buf_;           // 还没拼成完整一行的字节
    SSEEvent cur_;              // 正在拼的这个事件
    bool has_event_ = false;    // event: 是否已取过第一行
    bool has_id_ = false;       // id: 是否已取过第一行
    bool has_data_ = false;     // data: 是否至少出现过一行
    bool has_fields_ = false;   // cur_ 里有没有任何字段 → 决定空行要不要发射
};

} // namespace llm
