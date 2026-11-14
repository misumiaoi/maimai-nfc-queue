#pragma once

#include <stdint.h>

/** 一次贴卡只产生一次事件；连续多次检测不到卡后才重新允许刷卡。 */
class CardPresence {
 public:
  explicit CardPresence(uint8_t removalMisses) : removalMisses_(removalMisses) {}

  bool observe(bool present) {
    if (present) {
      misses_ = 0;
      if (waiting_) return false;
      waiting_ = true;
      return true;
    }
    if (waiting_ && ++misses_ >= removalMisses_) {
      waiting_ = false;
      misses_ = 0;
    }
    return false;
  }

  bool waitingForRemoval() const { return waiting_; }

 private:
  uint8_t removalMisses_;
  uint8_t misses_ = 0;
  bool waiting_ = false;
};
