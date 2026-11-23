// D1 占位：D4 会被替换成真正的流式 CLI（参数解析 -> chatStream -> 逐字打印）。
#include <cstdio>

int main() {
    std::fprintf(stderr,
                 "llmchat: D1 骨架，功能在 D4 实现（现在只会打印这行）。\n");
    return 0;
}
