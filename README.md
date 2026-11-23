# llm-sdk

C++17 大模型接入 SDK（W1）。不用任何官方 SDK，只用 libcurl 自己实现 HTTP + SSE 层，
把「用户消息」翻译成「模型逐字返回的流」。

## 当前状态

D1 骨架：只有统一 IR 的三个头文件与 6 个占位翻译单元，**还没有任何可用功能**。

## 编译

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build -j

完整说明（用法 / 目录 / 验收）在 D5 补齐。
