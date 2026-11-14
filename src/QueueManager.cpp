#include "QueueManager.h"

#include <string.h>

namespace {

constexpr uint8_t kMagic0 = 'Q';
constexpr uint8_t kMagicVersion2 = '2';
constexpr uint8_t kMagicVersion1 = '1';

/** v2 每条占的字节数：散列 + 数字。 */
constexpr size_t kEntryBytes = 8 + kTailDigits;

/** v1 每条占的字节数：散列 + 6 位数字。 */
constexpr size_t kEntryBytesV1 = 8 + 6;

/** CRC-16/CCITT-FALSE。 */
uint16_t crc16(const uint8_t* p, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; ++i) {
    crc ^= static_cast<uint16_t>(p[i]) << 8;
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

}  // namespace

QueueManager::QueueManager(uint32_t confirmWindowMs, uint8_t machineCount)
    : confirmWindowMs_(confirmWindowMs),
      machineCount_(machineCount == 0 ? 1 : machineCount) {
  memset(entries_, 0, sizeof(entries_));
}

int QueueManager::indexOf(uint64_t hash) const {
  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].hash == hash) return static_cast<int>(i);
  }
  return -1;
}

void QueueManager::clearEntry(size_t index) {
  memset(&entries_[index], 0, sizeof(Entry));
}

void QueueManager::removeAtInternal(size_t index) {
  for (size_t i = index; i + 1 < count_; ++i) {
    entries_[i] = entries_[i + 1];
  }
  --count_;
  clearEntry(count_);
}

TapResult QueueManager::handleTap(const CardId& id, uint32_t nowMs) {
  const int idx = indexOf(id.hash);

  if (idx >= 0) {
    Entry& e = entries_[static_cast<size_t>(idx)];
    const bool confirmed = e.pending && (nowMs - e.pendingSinceMs) <= confirmWindowMs_;
    if (confirmed) {
      removeAtInternal(static_cast<size_t>(idx));
      refreshPlaySlots(nowMs);
      return {TapOutcome::Left, idx + 1};
    }
    e.pending = true;
    e.pendingSinceMs = nowMs;
    return {TapOutcome::ConfirmLeave, idx + 1};
  }

  if (count_ >= kMaxQueue) return {TapOutcome::QueueFull, 0};

  Entry& e = entries_[count_];
  e.hash = id.hash;
  memcpy(e.tail, id.tail, sizeof(e.tail));
  e.joinedMs = nowMs;
  e.playing = false;
  e.playSinceMs = 0;
  e.pending = false;
  e.pendingSinceMs = 0;
  ++count_;
  refreshPlaySlots(nowMs);
  return {TapOutcome::Joined, static_cast<int>(count_)};
}

bool QueueManager::expirePending(uint32_t nowMs) {
  bool changed = false;
  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].pending && (nowMs - entries_[i].pendingSinceMs) > confirmWindowMs_) {
      entries_[i].pending = false;
      entries_[i].pendingSinceMs = 0;
      changed = true;
    }
  }
  return changed;
}

size_t QueueManager::expireStale(uint32_t nowMs, uint32_t staleMs) {
  if (staleMs == 0) return 0;

  size_t removed = 0;
  size_t i = 0;
  while (i < count_) {
    const uint32_t since = entries_[i].joinedMs;
    if (since != 0 && (nowMs - since) > staleMs) {
      removeAtInternal(i);
      ++removed;
    } else {
      ++i;
    }
  }
  if (removed > 0) refreshPlaySlots(nowMs);
  return removed;
}

void QueueManager::claimStartupTimestamps(uint32_t nowMs) {
  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].joinedMs == 0) entries_[i].joinedMs = nowMs;
  }
  refreshPlaySlots(nowMs);
}

void QueueManager::refreshPlaySlots(uint32_t nowMs) {
  const size_t slots = static_cast<size_t>(machineCount_) * 2;
  for (size_t i = 0; i < count_; ++i) {
    const bool shouldPlay = i < slots;
    if (shouldPlay && !entries_[i].playing) {
      entries_[i].playing = true;
      entries_[i].playSinceMs = nowMs;
    } else if (!shouldPlay && entries_[i].playing) {
      entries_[i].playing = false;
      entries_[i].playSinceMs = 0;
    }
  }
}

bool QueueManager::removeHead() { return removeAt(0); }

bool QueueManager::removeAt(size_t index) {
  if (index >= count_) return false;
  removeAtInternal(index);
  return true;
}

bool QueueManager::hasPending() const {
  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].pending) return true;
  }
  return false;
}

int QueueManager::pendingIndex() const {
  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].pending) return static_cast<int>(i);
  }
  return -1;
}

uint32_t QueueManager::playingForMs(size_t i, uint32_t nowMs) const {
  if (i >= count_ || !entries_[i].playing || entries_[i].playSinceMs == 0) return 0;
  return nowMs - entries_[i].playSinceMs;
}

size_t QueueManager::labelLength(size_t i) const {
  for (size_t len = 4; len < kTailDigits; ++len) {
    bool unique = true;
    for (size_t j = 0; j < count_ && unique; ++j) {
      if (j == i) continue;
      if (strncmp(entries_[j].tail + kTailDigits - len, entries_[i].tail + kTailDigits - len,
                  len) == 0) {
        unique = false;
      }
    }
    if (unique) return len;
  }
  return kTailDigits;
}

void QueueManager::label(size_t i, char* out) const {
  const size_t len = labelLength(i);
  memcpy(out, entries_[i].tail + kTailDigits - len, len);
  out[len] = '\0';
}

size_t QueueManager::serialize(uint8_t* buf, size_t cap) const {
  const size_t body = 3 + count_ * kEntryBytes;
  const size_t need = body + 2;
  if (cap < need) return 0;

  buf[0] = kMagic0;
  buf[1] = kMagicVersion2;
  buf[2] = static_cast<uint8_t>(count_);
  uint8_t* p = buf + 3;
  for (size_t i = 0; i < count_; ++i) {
    for (int b = 0; b < 8; ++b) {
      *p++ = static_cast<uint8_t>(entries_[i].hash >> (8 * b));
    }
    memcpy(p, entries_[i].tail, kTailDigits);
    p += kTailDigits;
  }
  const uint16_t crc = crc16(buf, body);
  buf[body] = static_cast<uint8_t>(crc & 0xFF);
  buf[body + 1] = static_cast<uint8_t>(crc >> 8);
  return need;
}

bool QueueManager::deserialize(const uint8_t* buf, size_t len) {
  if (buf == nullptr || len < 3 || buf[0] != kMagic0) return false;

  const uint8_t version = buf[1];
  if (version != kMagicVersion2 && version != kMagicVersion1) return false;

  const bool legacy = version == kMagicVersion1;
  const size_t entryBytes = legacy ? kEntryBytesV1 : kEntryBytes;
  const size_t n = buf[2];
  if (n > kMaxQueue) return false;

  const size_t body = 3 + n * entryBytes;
  if (legacy) {
    if (len != body) return false;
  } else {
    if (len != body + 2) return false;
    const uint16_t want = static_cast<uint16_t>(buf[body]) |
                          static_cast<uint16_t>(static_cast<uint16_t>(buf[body + 1]) << 8);
    if (crc16(buf, body) != want) return false;
  }

  Entry loaded[kMaxQueue];
  memset(loaded, 0, sizeof(loaded));
  const uint8_t* p = buf + 3;
  for (size_t i = 0; i < n; ++i) {
    uint64_t h = 0;
    for (int b = 0; b < 8; ++b) {
      h |= static_cast<uint64_t>(*p++) << (8 * b);
    }
    loaded[i].hash = h;

    if (legacy) {
      // v1 只存了末尾 6 位，左补 0 凑满显示宽度；默认还是显示 4 位，
      // 只有极端的重号情况才会露出补位。
      memset(loaded[i].tail, '0', kTailDigits);
      memcpy(loaded[i].tail + (kTailDigits - 6), p, 6);
      p += 6;
    } else {
      memcpy(loaded[i].tail, p, kTailDigits);
      p += kTailDigits;
    }
    loaded[i].tail[kTailDigits] = '\0';

    for (size_t j = 0; j < i; ++j) {
      if (loaded[j].hash == loaded[i].hash) return false;  // 重复条目视为损坏
    }
  }

  memcpy(entries_, loaded, sizeof(entries_));
  count_ = n;
  return true;
}