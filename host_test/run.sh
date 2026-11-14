#!/usr/bin/env bash
# 在电脑上编译并运行不依赖硬件的逻辑测试（卡号解析、队列状态机、序列化格式）。
# 需要 g++ 支持 C++17 和 ASan/UBSan。
set -euo pipefail
cd "$(dirname "$0")/.."
g++ -std=c++17 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -Isrc \
  host_test/test_logic.cpp src/CardId.cpp src/TUnion.cpp src/QueueManager.cpp \
  -o /tmp/maimai_queue_test
/tmp/maimai_queue_test