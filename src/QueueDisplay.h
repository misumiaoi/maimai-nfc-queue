#pragma once

#include <stdint.h>

#include "NameRegistry.h"
#include "QueueManager.h"

/**
 * 4.2 寸黑白墨水屏（400x300）上的队列显示。
 *
 * 平时用局部刷新（快、不闪），每 config::kFullRefreshEvery 次做一次整屏刷新来清残影。
 * 字体是 Adafruit_GFX 自带的 ASCII 字体。绑了名字的行显示名字，否则显示卡号末几位。
 * 要显示中文，换成 U8g2_for_Adafruit_GFX 加 CJK 字体即可，布局不用改。
 */
class QueueDisplay {
 public:
  /** 初始化 SPI 和屏幕。 */
  void begin();

  /**
   * 重画整个队列页面。
   *
   * 布局：顶部标题和等待人数；中间每行一个位置（前 2 * 机台数 行标 PLAY）；
   * 底部是提示，以及登记用的热点名。处于“待确认退出”的行反色显示。
   *
   * @param queue   要显示的队列。
   * @param names   用来把散列换成显示名。
   * @param message 底部一次性提示，如 "JOINED #3"；传 nullptr 则按状态显示默认提示。
   */
  void render(const QueueManager& queue, const NameRegistry& names, const char* message);

  /**
   * 整屏提示：名字已经提交，正在等这张卡贴上来。
   *
   * @param name 规范化后的显示名。
   */
  void showBindPrompt(const char* name);

  /**
   * 显示致命错误（例如读卡器没接好），不依赖队列。
   *
   * @param message 显示的错误文本。
   */
  void showFatal(const char* message);

 private:
  void drawQueue(const QueueManager& queue, const NameRegistry& names, const char* message);

  uint16_t refreshCount_ = 0;
};
