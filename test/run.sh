#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
g++ -std=c++17 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -Isrc \
  test/test_logic.cpp src/CardId.cpp src/TUnion.cpp src/QueueManager.cpp \
  -o "$test_dir/logic"
"$test_dir/logic"
g++ -std=c++17 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -Itest/fakes -Isrc \
  test/test_device.cpp src/CardId.cpp src/QueueManager.cpp src/QueueStorage.cpp \
  -o "$test_dir/device"
"$test_dir/device"
