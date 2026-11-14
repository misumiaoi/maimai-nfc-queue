#pragma once

#include <stddef.h>
#include <stdint.h>

#include "CardId.h"

/** 队列最多容纳多少张卡。 */
constexpr size_t kMaxQueue = 24;

/** 队列里的一项：卡的散列和用于显示的末尾数字。 */
struct Entry {
  uint64_t hash;
  char tail[kTailDigits + 1];
};

/** 一次刷卡处理后的结果类型。 */
enum class TapOutcome {
  Joined,        ///< 卡不在队列里，已加入队尾。
  QueueFull,     ///< 卡不在队列里，但队列已满，没有加入。
  ConfirmLeave,  ///< 卡已在队列里，等待在确认窗口内再刷一次来退出。
  Left,          ///< 确认窗口内再次刷卡，已退出。
};

/** handleTap 的返回值。 */
struct TapResult {
  TapOutcome outcome;
  int position;  ///< 1 起始的位置：Joined 时是新位置，ConfirmLeave / Left 时是当前或原位置，QueueFull 时为 0。
};

/**
 * 排队逻辑，不依赖任何硬件，时间由调用方传入。
 *
 * 规则：
 *  - 卡不在队列：刷一次加入队尾。
 *  - 卡在队列：第一次刷进入“待确认退出”，在确认窗口内再刷同一张卡才真正退出。
 *  - 待确认状态同一时刻只有一张卡；另一张卡刷卡会清除它。
 *  - 窗口超时后待确认状态失效，不改变队列。
 */
class QueueManager {
 public:
  /**
   * @param confirmWindowMs 刷第一次之后，等待第二次刷卡的时间（毫秒）。
   */
  explicit QueueManager(uint32_t confirmWindowMs);

  /**
   * 处理一次刷卡。
   *
   * @param id    被刷卡的身份。
   * @param nowMs 当前时间（毫秒，允许计数回绕）。
   * @return 本次刷卡的结果和位置。
   */
  TapResult handleTap(const CardId& id, uint32_t nowMs);

  /**
   * 检查待确认退出是否已超时，超时则清除。
   *
   * @param nowMs 当前时间（毫秒）。
   * @return 本次调用清除了待确认状态返回 true，调用方据此刷新屏幕。
   */
  bool expirePending(uint32_t nowMs);

  /**
   * 移除队首，供现场按钮使用（队首的人走了但没退出）。
   *
   * @return 队列非空并已移除返回 true。
   */
  bool removeHead();

  /** 队列当前长度。 */
  size_t size() const { return count_; }

  /** 取第 i 项（0 起始），调用方保证 i < size()。 */
  const Entry& at(size_t i) const { return entries_[i]; }

  /** 是否存在待确认退出的卡。 */
  bool hasPending() const { return pendingActive_; }

  /** 待确认退出的卡在队列中的下标，不存在时返回 -1。 */
  int pendingIndex() const;

  /**
   * 第 i 项在屏幕上要显示的数字位数。
   *
   * 默认 4 位；如果另一项的末尾 4 位相同，就逐位加长，最多 6 位，直到能区分为止。
   */
  size_t labelLength(size_t i) const;

  /**
   * 生成第 i 项的显示文本（卡号末尾若干位）。
   *
   * @param i   队列下标。
   * @param out 输出缓冲区，至少 kTailDigits + 1 字节。
   */
  void label(size_t i, char* out) const;

  /**
   * 把队列序列化成字节串，用于保存到闪存。待确认状态不保存。
   *
   * @param buf 输出缓冲区。
   * @param cap 缓冲区容量。
   * @return 写入的字节数；容量不足返回 0。
   */
  size_t serialize(uint8_t* buf, size_t cap) const;

  /**
   * 从字节串恢复队列，格式不合法时保持队列原样。
   *
   * @return 恢复成功返回 true。
   */
  bool deserialize(const uint8_t* buf, size_t len);

  /** 序列化后的最大字节数。 */
  static constexpr size_t kMaxSerializedBytes = 3 + kMaxQueue * (8 + kTailDigits);

 private:
  int indexOf(uint64_t hash) const;
  void removeAt(size_t index);

  Entry entries_[kMaxQueue];
  size_t count_ = 0;
  uint32_t confirmWindowMs_;
  bool pendingActive_ = false;
  uint64_t pendingHash_ = 0;
  uint32_t pendingSinceMs_ = 0;
};
