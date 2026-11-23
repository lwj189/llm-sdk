cmake_minimum_required(VERSION 3.16)
project(llm_sdk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(CURL REQUIRED)
find_package(nlohmann_json QUIET)

add_library(llmcore STATIC
    src/sse.cpp
    src/http_client.cpp
    src/providers/openai_compat.cpp
    src/providers/deepseek.cpp
)
target_include_directories(llmcore PUBLIC include)
target_link_libraries(llmcore PUBLIC CURL::libcurl)
if (nlohmann_json_FOUND)
    target_link_libraries(llmcore PUBLIC nlohmann_json::nlohmann_json)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann/json.hpp)
    # 可选：单头模式，把 json.hpp 放这里就不用装 apt 包
    target_include_directories(llmcore PUBLIC third_party)
else()
    message(FATAL_ERROR "缺少 nlohmann/json：sudo apt install nlohmann-json3-dev")
endif()

add_executable(llmchat examples/cli_chat.cpp)
target_link_libraries(llmchat PRIVATE llmcore)

enable_testing()
add_executable(llm_tests tests/test_sse.cpp)
target_link_libraries(llm_tests PRIVATE llmcore)
add_test(NAME sse COMMAND llm_tests)
