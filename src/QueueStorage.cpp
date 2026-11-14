#include "QueueStorage.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

namespace {

Preferences g_prefs;
const char* const kNamespace = "mai-queue";
const char* const kKeyQueue = "queue";
const char* const kKeyNames = "names";
const char* const kKeySalt = "salt";

}  // namespace

void QueueStorage::begin() { g_prefs.begin(kNamespace, false); }

uint64_t QueueStorage::loadOrCreateSalt() {
  uint64_t salt = g_prefs.getULong64(kKeySalt, 0);
  if (salt == 0) {
    salt = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
    if (salt == 0) salt = 1;
    g_prefs.putULong64(kKeySalt, salt);
  }
  return salt;
}

bool QueueStorage::load(QueueManager& queue) {
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

bool QueueStorage::load(NameRegistry& names) {
  size_t len = g_prefs.getBytesLength(kKeyNames);
  if (len == 0 || len > NameRegistry::kMaxSerializedBytes) return false;

  uint8_t buf[NameRegistry::kMaxSerializedBytes];
  if (g_prefs.getBytes(kKeyNames, buf, len) != len) return false;
  return names.deserialize(buf, len);
}

bool QueueStorage::save(const NameRegistry& names) {
  uint8_t buf[NameRegistry::kMaxSerializedBytes];
  size_t n = names.serialize(buf, sizeof(buf));
  if (n == 0) return false;
  return g_prefs.putBytes(kKeyNames, buf, n) == n;
}
