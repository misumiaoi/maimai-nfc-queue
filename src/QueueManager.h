#pragma once

#include <stddef.h>
#include <stdint.h>

#include "CardId.h"

/** 队列最多容纳多少张卡。 */
constexpr size_t kMaxQueue = 24;

/**
 * 队列里的一项。
 *
 * 只有 hash 和 tail 会存进闪存；其余字段都是本次运行内的状态，
 * 重启后由 QueueManager 重新推算。
 */
struct Entry {
  uint64_t hash;                ///< 卡的散列，用来判断“是不是同一张卡”。
  char tail[kTailDigits + 1];   ///< 卡号末尾数字，用来显示。
  uint32_t joinedMs;            ///< 入队时刻。0 表示“本次运行还没记过”。
  uint32_t playSinceMs;         ///< 进入上机位的时刻。
  bool playing;                 ///< 是否在上机位（前 2 * 机台数）。
  uint32_t pendingSinceMs;      ///< 第一次刷卡的时刻，用于确认退出。
  bool pending;                 ///< 是否处于待确认退出状态。
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
 *  - 待确认状态是每张卡各记各的，别人刷卡不会把你这张的确认清掉；
 *    超时后单条失效，不影响队列。
 *  - 前 2 * 机台数 的条目算“上机位”，进入时开始计时，供屏幕提示用。
 */
class QueueManager {
 public:
  /**
   * @param confirmWindowMs 刷第一次之后，等待第二次刷卡的时间（毫秒）。
   * @param machineCount    机台数，用来决定上机位有几个。
   */
  explicit QueueManager(uint32_t confirmWindowMs, uint8_t machineCount = 1);

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
   * 清理停留过久的条目（例如有人排到一半走了，或者设备断电前留下的旧数据）。
   *
   * @param nowMs   当前时间（毫秒）。
   * @param staleMs 允许的最长停留时间；传 0 表示不启用。
   * @return 被移除的条目数量。
   */
  size_t expireStale(uint32_t nowMs, uint32_t staleMs);

  /**
   * 把入队时刻为 0 的条目记为现在。
   *
   * 队列不保存时间戳，所以从闪存恢复出来的条目要先调用一次，
   * 否则停留时间会算成从开机起的时长。
   */
  void claimStartupTimestamps(uint32_t nowMs);

  /**
   * 重新推算上机位，并给刚进入上机位的条目开始计时。
   *
   * 插入、移除之后会自动调用；从闪存恢复后由调用方调一次。
   */
  void refreshPlaySlots(uint32_t nowMs);

  /**
   * 移除队首，供现场按钮使用（队首的人走了但没退出）。
   *
   * @return 队列非空并已移除返回 true。
   */
  bool removeHead();

  /**
   * 移除指定位置，供现场按钮使用（队伍中间的人走了）。
   *
   * @param index 0 起始的位置。
   * @return 位置合法并已移除返回 true。
   */
  bool removeAt(size_t index);

  /** 队列当前长度。 */
  size_t size() const { return count_; }

  /** 取第 i 项（0 起始），调用方保证 i < size()。 */
  const Entry& at(size_t i) const { return entries_[i]; }

  /** 是否存在待确认退出的卡。 */
  bool hasPending() const;

  /** 第 i 项是否处于待确认退出状态。 */
  bool isPending(size_t i) const { return i < count_ && entries_[i].pending; }

  /** 第一个待确认退出的位置，不存在时返回 -1。 */
  int pendingIndex() const;

  /** 第 i 项是否在上机位。 */
  bool isPlaying(size_t i) const { return i < count_ && entries_[i].playing; }

  /** 第 i 项在上机位停留了多久（毫秒），不在上机位返回 0。 */
  uint32_t playingForMs(size_t i, uint32_t nowMs) const;

  /**
   * 第 i 项在屏幕上要显示的数字位数。
   *
   * 默认 4 位；如果另一项的末尾 4 位相同，就逐位加长，最多 8 位，直到能区分为止。
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
   * 把队列序列化成字节串，用于保存到闪存。时间状态不保存。
   *
   * 格式（v2）：'Q' '2' count，随后每条 8 字节散列 + kTailDigits 字节数字，
   * 末尾 2 字节是前面内容的 CRC-16/CCITT-FALSE（小端）。
   *
   * @param buf 输出缓冲区。
   * @param cap 缓冲区容量。
   * @return 写入的字节数；容量不足返回 0。
   */
  size_t serialize(uint8_t* buf, size_t cap) const;

  /**
   * 从字节串恢复队列，格式不合法时保持队列原样。
   *
   * 同时接受 v1（'Q' '1'，无校验）以便从旧固件升级；
   * 重复的散列、长度不对、校验错的内容都拒绝。
   *
   * @return 恢复成功返回 true。
   */
  bool deserialize(const uint8_t* buf, size_t len);

  /** 序列化后的最大字节数。 */
  static constexpr size_t kMaxSerializedBytes = 5 + kMaxQueue * (8 + kTailDigits);

 private:
  int indexOf(uint64_t hash) const;
  void removeAtInternal(size_t index);
  void clearEntry(size_t index);

  Entry entries_[kMaxQueue];
  size_t count_ = 0;
  uint32_t confirmWindowMs_;
  uint8_t machineCount_;
};