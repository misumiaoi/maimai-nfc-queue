#pragma once

#include <stdint.h>

#include "QueueManager.h"

/**
 * 把队列、设备盐和店员卡保存到 ESP32 内部闪存（NVS），断电重启后恢复。
 *
 * 只在队列变化时写入。NVS 自带磨损均衡，按一天几百次刷卡的量级不会成为问题。
 */
class QueueStorage {
 public:
  /** 打开存储区，必须在其他方法之前调用一次。 */
  void begin();

  /**
   * 读取本机随机盐，首次启动时生成并保存。
   *
   * 盐用来给卡号做散列，每台设备不同。
   */
  uint64_t loadOrCreateSalt();

  /**
   * 从闪存恢复队列。
   *
   * @param queue 输出：恢复后的队列。没有保存过或数据损坏时保持原样。
   * @return 成功恢复返回 true。
   */
  bool load(QueueManager& queue);

  /**
   * 把队列写入闪存。
   *
   * @return 写入成功返回 true。
   */
  bool save(const QueueManager& queue);

  /**
   * 读取店员卡散列，没设置过返回 0。
   *
   * 设置过之后，长按按钮移人需要先刷这张卡。
   */
  uint64_t loadAdminHash();

  /**
   * 保存店员卡散列，覆盖旧值。
   *
   * @return 写入成功返回 true。
   */
  bool saveAdminHash(uint64_t hash);

  /** 清除店员卡，恢复到“任何人都能长按移人”的状态。 */
  bool clearAdminHash();
};