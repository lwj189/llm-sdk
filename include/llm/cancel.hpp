#pragma once
#include <atomic>

namespace llm {

// 取消标记定义
class CancelToken {
public:
    // 与说明书的差异：cancel()/cancelled() 在类内直接定义（inline），因为目录结构里
    // 没有 cancel.cpp。签名与语义不变：跨线程安全，cancelled() 只读。
    void cancel() { flag_.store(true, std::memory_order_relaxed); }
    bool cancelled() const { return flag_.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> flag_{false};
};

} // namespace llm
