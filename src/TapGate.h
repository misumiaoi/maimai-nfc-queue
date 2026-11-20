#pragma once

#include <stdint.h>

/**
 * 决定一次读卡要不要当成一次新的刷卡。不依赖硬件，时间由调用方传入。
 *
 * 规则：
 *  - 处理过一张卡以后进入“等待拿开”，期间读卡器只检查有没有卡，不处理刷卡。
 *  - 连续 missesToRelease 次检查不到卡，才认为卡已经拿开。没有时间上限，
 *    卡一直贴着就一直等，不会被当成第二次刷卡。
 *  - 同一张卡拿开后 cooldownMs 内又被读到，视为接触不良造成的抖动，忽略。
 *    被忽略的那次不会重新开始计算冷却时间。
 */
class TapGate {
 public:
  TapGate(uint8_t missesToRelease, uint32_t cooldownMs)
      : missesToRelease_(missesToRelease), cooldownMs_(cooldownMs) {}

  /** 是否在等待卡拿开。为 true 时调用方只做存在检查并调用 observe。 */
  bool waitingForRemoval() const { return waiting_; }

  /** 一张卡已经按刷卡处理（加入、退出、待确认等），开始等它拿开。 */
  void onHandled(uint64_t hash) {
    waiting_ = true;
    misses_ = 0;
    hasLast_ = true;
    lastHash_ = hash;
    stampOnRelease_ = true;
  }

  /** 读到的卡被忽略或无法识别，只等它拿开，不改变冷却状态。 */
  void onIgnored() {
    waiting_ = true;
    misses_ = 0;
    stampOnRelease_ = false;
  }

  /**
   * 等待期间报告一次存在检查的结果。
   *
   * @return 这次调用确认卡已拿开返回 true。
   */
  bool observe(bool present, uint32_t nowMs) {
    if (!waiting_) return false;
    if (present) {
      misses_ = 0;
      return false;
    }
    if (++misses_ < missesToRelease_) return false;
    waiting_ = false;
    if (stampOnRelease_) releasedAtMs_ = nowMs;
    return true;
  }

  /** 这张卡现在能不能当成新的刷卡处理。 */
  bool accepts(uint64_t hash, uint32_t nowMs) const {
    if (waiting_) return false;
    if (!hasLast_ || hash != lastHash_) return true;
    return (nowMs - releasedAtMs_) >= cooldownMs_;
  }

 private:
  uint8_t missesToRelease_;
  uint32_t cooldownMs_;
  bool waiting_ = false;
  uint8_t misses_ = 0;
  bool hasLast_ = false;
  uint64_t lastHash_ = 0;
  uint32_t releasedAtMs_ = 0;
  bool stampOnRelease_ = false;
};
