#include "QueueDisplay.h"

#include <Arduino.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <GxEPD2_BW.h>
#include <SPI.h>
#include <stdio.h>

#include "RowLabel.h"
#include "config.h"

namespace {

// 不同批次的 4.2 寸屏驱动不同。Waveshare 4.2" V2 / GoodDisplay GDEY042T81 用下面这个；
// 旧版 SSD1683 之前的屏幕改用 GxEPD2_420。
using PanelDriver = GxEPD2_420_GDEY042T81;
using Panel = GxEPD2_BW<PanelDriver, PanelDriver::HEIGHT>;

Panel g_epd(PanelDriver(config::kPinEpdCs, config::kPinEpdDc, config::kPinEpdRst,
                        config::kPinEpdBusy));

constexpr int kHeaderHeight = 40;
constexpr int kRowHeight = 26;
constexpr int kStatusY = 258;
constexpr int kHintY = 290;

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

}  // namespace

void QueueDisplay::begin() {
  SPI.begin(config::kPinEpdSck, -1, config::kPinEpdMosi, config::kPinEpdCs);
  g_epd.init(115200, true, 2, false);
  g_epd.setRotation(0);
}

void QueueDisplay::render(const QueueManager& queue, const NameRegistry& names,
                          const char* message) {
  const bool full = (refreshCount_ % config::kFullRefreshEvery) == 0;
  ++refreshCount_;

  if (full) {
    g_epd.setFullWindow();
  } else {
    g_epd.setPartialWindow(0, 0, g_epd.width(), g_epd.height());
  }
  g_epd.firstPage();
  do {
    drawQueue(queue, names, message);
  } while (g_epd.nextPage());
}

void QueueDisplay::showBindPrompt(const char* name) {
  g_epd.setFullWindow();
  g_epd.firstPage();
  do {
    g_epd.fillScreen(GxEPD_WHITE);
    drawText(&FreeMonoBold18pt7b, 8, 90, GxEPD_BLACK, "TAP CARD");
    drawText(&FreeMonoBold18pt7b, 8, 150, GxEPD_BLACK, name != nullptr ? name : "");
    drawText(&FreeMonoBold12pt7b, 8, 200, GxEPD_BLACK, "BINDS THIS NAME");
    drawText(&FreeMonoBold12pt7b, 8, 240, GxEPD_BLACK, "DOES NOT JOIN");
  } while (g_epd.nextPage());
}

void QueueDisplay::showFatal(const char* message) {
  g_epd.setFullWindow();
  g_epd.firstPage();
  do {
    g_epd.fillScreen(GxEPD_WHITE);
    drawText(&FreeMonoBold18pt7b, 10, 120, GxEPD_BLACK, "ERROR");
    drawText(&FreeMonoBold12pt7b, 10, 170, GxEPD_BLACK, message);
  } while (g_epd.nextPage());
}

void QueueDisplay::drawQueue(const QueueManager& queue, const NameRegistry& names,
                             const char* message) {
  const int width = g_epd.width();
  g_epd.fillScreen(GxEPD_WHITE);

  char text[48];

  drawText(&FreeMonoBold18pt7b, 8, 28, GxEPD_BLACK, "MAIMAI QUEUE");
  snprintf(text, sizeof(text), "%u", static_cast<unsigned>(queue.size()));
  drawText(&FreeMonoBold18pt7b, width - 8 - textWidth(&FreeMonoBold18pt7b, text), 28,
           GxEPD_BLACK, text);
  g_epd.drawLine(0, kHeaderHeight - 4, width, kHeaderHeight - 4, GxEPD_BLACK);

  const size_t playSlots = static_cast<size_t>(config::kMachineCount) * 2;
  const size_t visible = queue.size() < config::kVisibleRows ? queue.size() : config::kVisibleRows;
  const int pending = queue.pendingIndex();
  const GFXfont* rowFont = &FreeMonoBold12pt7b;

  for (size_t i = 0; i < visible; ++i) {
    const int top = kHeaderHeight + static_cast<int>(i) * kRowHeight;
    const int baseline = top + 20;
    const bool inverted = static_cast<int>(i) == pending;
    const uint16_t fg = inverted ? GxEPD_WHITE : GxEPD_BLACK;

    if (inverted) g_epd.fillRect(0, top, width, kRowHeight, GxEPD_BLACK);

    char label[kRowLabelCap];
    formatRowLabel(queue, names, i, label);
    snprintf(text, sizeof(text), "%2u  %s", static_cast<unsigned>(i + 1), label);
    drawText(rowFont, 8, baseline, fg, text);

    if (i < playSlots) {
      const int playW = textWidth(rowFont, "PLAY");
      const int playX = width - 8 - playW;
      if (8 + textWidth(rowFont, text) + 8 < playX) {
        drawText(rowFont, playX, baseline, fg, "PLAY");
      }
    }
    if (i + 1 == playSlots && queue.size() > playSlots && !inverted) {
      g_epd.drawLine(0, top + kRowHeight - 1, width, top + kRowHeight - 1, GxEPD_BLACK);
    }
  }

  g_epd.drawLine(0, kStatusY - 18, width, kStatusY - 18, GxEPD_BLACK);
  const char* footer = message;
  if (footer == nullptr) {
    footer = queue.hasPending() ? "TAP AGAIN TO LEAVE" : "TAP CARD TO JOIN";
  }
  drawText(&FreeMonoBold12pt7b, 8, kStatusY, GxEPD_BLACK, footer);

  snprintf(text, sizeof(text), "WIFI %s", config::kWifiSsid);
  drawText(&FreeMonoBold9pt7b, 8, kHintY, GxEPD_BLACK, text);
  if (queue.size() > visible) {
    char more[16];
    snprintf(more, sizeof(more), "+%u MORE", static_cast<unsigned>(queue.size() - visible));
    drawText(&FreeMonoBold9pt7b, width - 8 - textWidth(&FreeMonoBold9pt7b, more), kHintY,
             GxEPD_BLACK, more);
  }
}
