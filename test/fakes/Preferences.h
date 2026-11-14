#pragma once
#include <stdint.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

// 故障注入仅用于电脑端测试；固件仍使用 ESP32 Preferences。
class Preferences {
 public:
  inline static std::map<std::string, std::vector<uint8_t>> data;
  inline static bool failOpen = false;
  inline static bool failWrite = false;
  inline static bool failRead = false;
  bool begin(const char*, bool) { return !failOpen; }
  bool isKey(const char* key) { return data.count(key) != 0; }
  size_t getBytesLength(const char* key) { return data[key].size(); }
  size_t getBytes(const char* key, void* out, size_t len) {
    if (failRead || !isKey(key) || data[key].size() != len) return 0;
    memcpy(out, data[key].data(), len);
    return len;
  }
  size_t putBytes(const char* key, const void* value, size_t len) {
    if (failWrite) return 0;
    const auto* bytes = static_cast<const uint8_t*>(value);
    data[key] = std::vector<uint8_t>(bytes, bytes + len);
    return len;
  }
  uint64_t getULong64(const char* key, uint64_t fallback) {
    uint64_t value;
    return getBytes(key, &value, sizeof(value)) == sizeof(value) ? value : fallback;
  }
  size_t putULong64(const char* key, uint64_t value) {
    return putBytes(key, &value, sizeof(value));
  }
  static void reset() {
    data.clear();
    failOpen = failWrite = failRead = false;
  }
};
