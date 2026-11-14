#include "QueueDisplay.h"

#include <Arduino.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <GxEPD2_BW.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>

namespace {

// 不同批次的 4.2 寸屏驱动不同。Waveshare 4.2" V2 / GoodDisplay GDEY042T81 用下面这个；
// 旧版 SSD1683 之前的屏幕改用 GxEPD2_420。
using PanelDriver = GxEPD2_420_GDEY042T81;
using Panel = GxEPD2_BW<PanelDriver, PanelDriver::HEIGHT>;

Panel g_epd(PanelDriver(config::kPinEpdCs, config::kPinEpdDc, config::kPinEpdRst,
                        config::kPinEpdBusy));

constexpr int kHeaderHeight = 40;
constexpr int kRowHeight = 30;
constexpr int kFooterTop = 274;
constexpr int kFooterY = 292;

/** 以给定基线位置画一段文字。 */
void drawText(const GFXfont* font, int x, int baselineY, uint16_t color, const char* text) {
  g_epd.setFont(font);
  g_epd.setTextColor(color);
  g_epd.setCursor(x, baselineY);
  g_epd.print(text);
}

/** 返回文字宽度，用来右对齐。 */
int textWidth(const GFXfont* font, const char* text) {
  int16_t x1, y1;
  uint16_t w, h;
  g_epd.setFont(font);
  g_epd.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return w;
}

/** 把秒数格式化成上机时长，超过一位数分钟就用 "12m"+ 的形式省空间。 */
void formatElapsed(uint32_t ms, char* out, size_t cap) {
  const uint32_t minutes = ms / 60000UL;
  if (ms >= config::kPlayLimitMs) {
    snprintf(out, cap, ">%lum", static_cast<unsigned long>(config::kPlayLimitMs / 60000UL));
  } else {
    snprintf(out, cap, "%lum", static_cast<unsigned long>(minutes));
  }
}

}  // namespace

void QueueDisplay::begin() {
  SPI.begin(config::kPinEpdSck, -1, config::kPinEpdMosi, config::kPinEpdCs);
  g_epd.init(115200, true, 2, false);
  g_epd.setRotation(0);
  last_.valid = false;
}

void QueueDisplay::buildFrame(const QueueManager& queue, const char* message, int highlight,
                              Frame* out) const {
  memset(out, 0, sizeof(*out));
  out->valid = true;
  out->highlight = highlight;
  out->pending = queue.pendingIndex();

  const size_t n = queue.size();
  snprintf(out->header, sizeof(out->header), "%u waiting", static_cast<unsigned>(n));

  const size_t visible = n < config::kVisibleRows ? n : config::kVisibleRows;
  out->visible = static_cast<int>(visible);

  const uint32_t nowMs = millis();
  for (size_t i = 0; i < visible; ++i) {
    char label[kTailDigits + 1];
    queue.label(i, label);

    char clock[8] = "";
    if (queue.isPlaying(i)) {
      formatElapsed(queue.playingForMs(i, nowMs), clock, sizeof(clock));
    }

    snprintf(out->rows[i], sizeof(out->rows[i]), "%2u %s %s", static_cast<unsigned>(i + 1), label,
             clock);
  }

  if (message != nullptr) {
    snprintf(out->footer, sizeof(out->footer), "%s", message);
  } else if (queue.hasPending()) {
    snprintf(out->footer, sizeof(out->footer), "TAP AGAIN TO LEAVE");
  } else if (n > visible) {
    snprintf(out->footer, sizeof(out->footer), "+%u MORE  TAP TO JOIN",
             static_cast<unsigned>(n - visible));
  } else {
    snprintf(out->footer, sizeof(out->footer), "TAP CARD TO JOIN");
  }
}

void QueueDisplay::drawFrame(const Frame& frame) {
  const int width = g_epd.width();
  g_epd.fillScreen(GxEPD_WHITE);

  drawText(&FreeMonoBold18pt7b, 8, 28, GxEPD_BLACK, "MAIMAI QUEUE");
  drawText(&FreeMonoBold12pt7b, width - 8 - textWidth(&FreeMonoBold12pt7b, frame.header), 24,
           GxEPD_BLACK, frame.header);
  g_epd.drawLine(0, kHeaderHeight - 4, width, kHeaderHeight - 4, GxEPD_BLACK);

  for (int i = 0; i < frame.visible; ++i) {
    const int top = kHeaderHeight + i * kRowHeight;
    const int baseline = top + 24;
    const bool inverted = i == frame.pending || i == frame.highlight;
    const uint16_t fg = inverted ? GxEPD_WHITE : GxEPD_BLACK;

    if (inverted) g_epd.fillRect(0, top, width, kRowHeight, GxEPD_BLACK);
    drawText(&FreeMonoBold12pt7b, 8, baseline, fg, frame.rows[i]);
  }

  g_epd.drawLine(0, kFooterTop - 18, width, kFooterTop - 18, GxEPD_BLACK);
  drawText(&FreeMonoBold12pt7b, 8, kFooterY, GxEPD_BLACK, frame.footer);
}

void QueueDisplay::render(const QueueManager& queue, const char* message, int highlight) {
  Frame frame;
  buildFrame(queue, message, highlight, &frame);

  const bool fullRefresh = !last_.valid || (refreshCount_ % config::kFullRefreshEvery) == 0;

  if (fullRefresh) {
    g_epd.setFullWindow();
  } else {
    int top = 10000;
    int bottom = -1;

    if (strcmp(last_.header, frame.header) != 0) {
      top = 0;
      bottom = kHeaderHeight;
    }

    const int rows = last_.visible > frame.visible ? last_.visible : frame.visible;
    for (int i = 0; i < rows; ++i) {
      const char* before = i < last_.visible ? last_.rows[i] : "";
      const char* after = i < frame.visible ? frame.rows[i] : "";
      bool changed = strcmp(before, after) != 0;
      if (!changed) changed = (i == last_.pending) != (i == frame.pending);
      if (!changed) changed = (i == last_.highlight) != (i == frame.highlight);
      if (changed) {
        const int rowTop = kHeaderHeight + i * kRowHeight;
        if (rowTop < top) top = rowTop;
        if (rowTop + kRowHeight > bottom) bottom = rowTop + kRowHeight;
      }
    }

    if (strcmp(last_.footer, frame.footer) != 0) {
      if (kFooterTop - 18 < top) top = kFooterTop - 18;
      if (g_epd.height() > bottom) bottom = g_epd.height();
    }

    if (bottom <= top) {
      // 内容没变，不碰屏幕，省一次闪烁和几百毫秒。
      return;
    }

    // 墨水屏按 8 像素一页寻址，窗口边界不对会出现错行，这里先对齐。
    top &= ~7;
    bottom = (bottom + 7) & ~7;
    if (bottom > g_epd.height()) bottom = g_epd.height();

    g_epd.setPartialWindow(0, top, g_epd.width(), bottom - top);
  }

  ++refreshCount_;
  g_epd.firstPage();
  do {
    drawFrame(frame);
  } while (g_epd.nextPage());

  last_ = frame;
}

void QueueDisplay::showFatal(const char* message) {
  last_.valid = false;
  g_epd.setFullWindow();
  g_epd.firstPage();
  do {
    g_epd.fillScreen(GxEPD_WHITE);
    drawText(&FreeMonoBold18pt7b, 10, 120, GxEPD_BLACK, "ERROR");
    drawText(&FreeMonoBold12pt7b, 10, 170, GxEPD_BLACK, message);
  } while (g_epd.nextPage());
}