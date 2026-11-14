#pragma once

#include <stdint.h>

#include "QueueManager.h"
#include "config.h"

/**
 * 4.2 寸黑白墨水屏（400x300）上的队列显示。
 *
 * 每次刷新都先算出整页的内容，再和上一次比：只有变化的行会重画，
 * 排版没动的时候连刷新都省掉。每 config::kFullRefreshEvery 次做一次整屏刷新来清残影。
 *
 * 字体是 Adafruit_GFX 自带的 ASCII 字体，只显示英文和数字；要显示中文，
 * 换成 U8g2_for_Adafruit_GFX 加 CJK 字体即可，布局不用改。
 */
class QueueDisplay {
 public:
  /** 初始化 SPI 和屏幕。 */
  void begin();

  /**
   * 重画队列页面。
   *
   * 布局：顶部标题和等待人数；中间每行一个号（上机位显示已经打了多久）；
   * 底部是提示。处于“待确认退出”的行反色显示，highlight 指定的行也反色。
   *
   * @param queue     要显示的队列。
   * @param message   底部一次性提示，如 "JOINED #3"；传 nullptr 则按状态显示默认提示。
   * @param highlight 需要反色强调的行，-1 表示没有。
   */
  void render(const QueueManager& queue, const char* message, int highlight = -1);

  /**
   * 显示致命错误（例如读卡器没接好），不依赖队列。
   *
   * @param message 显示的错误文本。
   */
  void showFatal(const char* message);

 private:
  /** 一屏的内容快照，用来做逐行比对。 */
  struct Frame {
    char header[24];
    char rows[config::kVisibleRows][24];
    char footer[32];
    int visible;
    int highlight;
    int pending;
    bool valid;
  };

  void buildFrame(const QueueManager& queue, const char* message, int highlight, Frame* out) const;
  void drawFrame(const Frame& frame);

  Frame last_;
  uint16_t refreshCount_ = 0;
};