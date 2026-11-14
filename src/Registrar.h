#pragma once

#include <stdint.h>

#include "NameRegistry.h"

/**
 * 一次登记：名字先到，下一张交通联合卡再绑上。
 *
 * 提交名字后进入等待。等待窗口内的下一次成功读卡把名字写入 NameRegistry，
 * 这一下不入队也不离队。超时、取消，或名字表写不进去时，队列规则保持原样。
 */
class Registrar {
 public:
  /** consume 对这一次刷卡的处理。 */
  enum class BindTap {
    None,     ///< 当前没在等待登记，调用方按普通入队/离队处理。
    Bound,    ///< 已把等待中的名字绑到这张卡，并结束等待。
    Full,     ///< 名字表已满，没有绑定，等待也结束。
    Expired,  ///< 这一下到来时窗口刚过，没有绑定，也不要当成入队。
  };

  /**
   * @param windowMs 提交名字之后，允许多长时间内贴卡（毫秒）。
   */
  explicit Registrar(uint32_t windowMs);

  /**
   * 开始（或改成）等待这个名字。name 会再规范化一次。
   *
   * @param nowMs 当前时间（毫秒，允许计数回绕）。
   * @return 名字合法并已进入等待返回 true。
   */
  bool arm(const char* name, uint32_t nowMs);

  /** 取消等待。没有等待时无效果。 */
  void cancel();

  /**
   * 窗口到了就清除等待。
   *
   * @return 这次调用清除了等待返回 true，调用方据此把屏幕从“请贴卡”改回去。
   */
  bool expire(uint32_t nowMs);

  /** 是否正在等一张卡。 */
  bool armed() const { return armed_; }

  /** 正在等待的名字；没在等待时返回 nullptr。 */
  const char* pending() const { return armed_ ? pending_ : nullptr; }

  /**
   * 若正在等待，则尝试把名字绑到这张卡的散列上。
   *
   * 绑定成功或名字表已满时都会结束等待。窗口已过时结束等待并返回 Expired。
   */
  BindTap consume(uint64_t hash, NameRegistry& names, uint32_t nowMs);

 private:
  bool timedOut(uint32_t nowMs) const;

  uint32_t windowMs_;
  bool armed_ = false;
  char pending_[kMaxNameLen + 1];
  uint32_t sinceMs_ = 0;
};
