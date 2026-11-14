#include "QueueStorage.h"

#include <Preferences.h>
#include <esp_system.h>

namespace {

Preferences g_prefs;
const char* const kNamespace = "mai-queue";
const char* const kKeyQueue = "queue";
const char* const kKeySalt = "salt";

}  // namespace

bool QueueStorage::begin() { return g_prefs.begin(kNamespace, false); }

bool QueueStorage::loadOrCreateSalt(uint64_t& salt) {
  if (g_prefs.isKey(kKeySalt)) {
    salt = g_prefs.getULong64(kKeySalt, 0);
    return salt != 0;
  }
  // 已有队列丢失盐时不能生成新盐，否则旧队列里的卡将无法再识别。
  if (g_prefs.isKey(kKeyQueue)) return false;
  salt = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
  if (salt == 0) salt = 1;
  return g_prefs.putULong64(kKeySalt, salt) == sizeof(salt);
}

bool QueueStorage::load(QueueManager& queue) {
  if (!g_prefs.isKey(kKeyQueue)) return true;
  size_t len = g_prefs.getBytesLength(kKeyQueue);
  if (len == 0 || len > QueueManager::kMaxSerializedBytes) return false;

  uint8_t buf[QueueManager::kMaxSerializedBytes];
  if (g_prefs.getBytes(kKeyQueue, buf, len) != len) return false;
  return queue.deserialize(buf, len);
}

bool QueueStorage::save(const QueueManager& queue) {
  uint8_t buf[QueueManager::kMaxSerializedBytes];
  size_t n = queue.serialize(buf, sizeof(buf));
  if (n == 0) return false;
  return g_prefs.putBytes(kKeyQueue, buf, n) == n;
}
