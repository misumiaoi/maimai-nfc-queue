#include "QueueManager.h"

#include <string.h>

namespace {

constexpr uint8_t kMagic0 = 'Q';
constexpr uint8_t kMagic1 = '1';
constexpr size_t kEntryBytes = 8 + kTailDigits;

}  // namespace

QueueManager::QueueManager(uint32_t confirmWindowMs) : confirmWindowMs_(confirmWindowMs) {
  memset(entries_, 0, sizeof(entries_));
}

int QueueManager::indexOf(uint64_t hash) const {
  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].hash == hash) return static_cast<int>(i);
  }
  return -1;
}

void QueueManager::removeAt(size_t index) {
  for (size_t i = index; i + 1 < count_; ++i) {
    entries_[i] = entries_[i + 1];
  }
  --count_;
  memset(&entries_[count_], 0, sizeof(Entry));
}

TapResult QueueManager::handleTap(const CardId& id, uint32_t nowMs) {
  int idx = indexOf(id.hash);

  if (idx >= 0) {
    bool confirmed = pendingActive_ && !pendingPaused_ && pendingHash_ == id.hash &&
                     (nowMs - pendingSinceMs_) <= confirmWindowMs_;
    if (confirmed) {
      removeAt(static_cast<size_t>(idx));
      pendingActive_ = false;
      return {TapOutcome::Left, idx + 1};
    }
    pendingActive_ = true;
    pendingPaused_ = false;
    pendingHash_ = id.hash;
    pendingSinceMs_ = nowMs;
    return {TapOutcome::ConfirmLeave, idx + 1};
  }

  pendingActive_ = false;
  if (count_ >= kMaxQueue) return {TapOutcome::QueueFull, 0};

  Entry& e = entries_[count_];
  e.hash = id.hash;
  memcpy(e.tail, id.tail, sizeof(e.tail));
  ++count_;
  return {TapOutcome::Joined, static_cast<int>(count_)};
}

bool QueueManager::expirePending(uint32_t nowMs) {
  if (pendingActive_ && !pendingPaused_ && (nowMs - pendingSinceMs_) > confirmWindowMs_) {
    pendingActive_ = false;
    return true;
  }
  return false;
}

void QueueManager::pausePending() {
  if (pendingActive_) pendingPaused_ = true;
}

void QueueManager::resumePending(uint32_t nowMs) {
  if (pendingActive_ && pendingPaused_) {
    pendingSinceMs_ = nowMs;
    pendingPaused_ = false;
  }
}

bool QueueManager::removeHead() {
  if (count_ == 0) return false;
  removeAt(0);
  pendingActive_ = false;
  return true;
}

int QueueManager::pendingIndex() const {
  return pendingActive_ ? indexOf(pendingHash_) : -1;
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
  size_t len = labelLength(i);
  memcpy(out, entries_[i].tail + kTailDigits - len, len);
  out[len] = '\0';
}

size_t QueueManager::serialize(uint8_t* buf, size_t cap) const {
  size_t need = 3 + count_ * kEntryBytes;
  if (cap < need) return 0;

  buf[0] = kMagic0;
  buf[1] = kMagic1;
  buf[2] = static_cast<uint8_t>(count_);
  uint8_t* p = buf + 3;
  for (size_t i = 0; i < count_; ++i) {
    for (int b = 0; b < 8; ++b) {
      *p++ = static_cast<uint8_t>(entries_[i].hash >> (8 * b));
    }
    memcpy(p, entries_[i].tail, kTailDigits);
    p += kTailDigits;
  }
  return need;
}

bool QueueManager::deserialize(const uint8_t* buf, size_t len) {
  if (len < 3 || buf[0] != kMagic0 || buf[1] != kMagic1) return false;
  size_t n = buf[2];
  if (n > kMaxQueue || len != 3 + n * kEntryBytes) return false;

  Entry loaded[kMaxQueue];
  memset(loaded, 0, sizeof(loaded));
  const uint8_t* p = buf + 3;
  for (size_t i = 0; i < n; ++i) {
    uint64_t h = 0;
    for (int b = 0; b < 8; ++b) {
      h |= static_cast<uint64_t>(*p++) << (8 * b);
    }
    loaded[i].hash = h;
    memcpy(loaded[i].tail, p, kTailDigits);
    loaded[i].tail[kTailDigits] = '\0';
    p += kTailDigits;
  }

  memcpy(entries_, loaded, sizeof(entries_));
  count_ = n;
  pendingActive_ = false;
  return true;
}
