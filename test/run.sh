#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
g++ -std=c++17 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -Isrc \
  test/test_logic.cpp src/CardId.cpp src/TUnion.cpp src/QueueManager.cpp \
  -o /tmp/maimai_queue_test
/tmp/maimai_queue_test
