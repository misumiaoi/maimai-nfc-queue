#include <Arduino.h>
#include <stdio.h>

#include "CardId.h"
#include "CardReader.h"
#include "QueueDisplay.h"
#include "QueueManager.h"
#include "QueueStorage.h"
#include "config.h"

namespace {

QueueManager g_queue(config::kConfirmWindowMs);
QueueStorage g_storage;
QueueDisplay g_display;
CardReader g_reader;
uint64_t g_salt = 0;

/**
 * 处理一次成功读到的交通联合卡：更新队列、保存、刷新屏幕，并等卡拿开。
 *
 * @param number 10 字节卡号。
 */
void onCard(const uint8_t number[kCardNumberBytes]) {
  const CardId id = makeCardId(number, g_salt);
  const TapResult r = g_queue.handleTap(id, millis());

  char message[32];
  switch (r.outcome) {
    case TapOutcome::Joined:
      snprintf(message, sizeof(message), "JOINED #%d", r.position);
      break;
    case TapOutcome::QueueFull:
      snprintf(message, sizeof(message), "QUEUE FULL");
      break;
    case TapOutcome::ConfirmLeave:
      snprintf(message, sizeof(message), "#%d: TAP AGAIN TO LEAVE", r.position);
      break;
    case TapOutcome::Left:
      snprintf(message, sizeof(message), "LEFT #%d", r.position);
      break;
  }

  if (r.outcome == TapOutcome::Joined || r.outcome == TapOutcome::Left) {
    g_storage.save(g_queue);
  }
  g_display.render(g_queue, message);
  g_reader.waitForRemoval(config::kRemovalWaitMs);
}

/** 处理读到卡但不是交通联合卡的情况：只提示，不改队列。 */
void onUnsupportedCard() {
  g_display.render(g_queue, "UNSUPPORTED CARD");
  g_reader.waitForRemoval(config::kRemovalWaitMs);
}

/**
 * 检查跳过按钮，长按达到 config::kSkipHoldMs 才移除队首，避免误触。
 *
 * 按下后会阻塞到松开，松开前读卡器不工作。
 */
void pollSkipButton() {
  if (digitalRead(config::kPinSkipButton) != LOW) return;

  const uint32_t start = millis();
  bool fired = false;
  while (digitalRead(config::kPinSkipButton) == LOW) {
    if (!fired && millis() - start >= config::kSkipHoldMs) {
      fired = true;
      if (g_queue.removeHead()) {
        g_storage.save(g_queue);
        g_display.render(g_queue, "HEAD REMOVED");
      }
    }
    delay(20);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(config::kCpuMhz);
  pinMode(config::kPinSkipButton, INPUT_PULLUP);

  g_storage.begin();
  g_salt = g_storage.loadOrCreateSalt();
  g_storage.load(g_queue);

  g_display.begin();
  if (!g_reader.begin()) {
    g_display.showFatal("PN532 NOT FOUND");
    while (true) delay(1000);
  }
  g_display.render(g_queue, nullptr);
}

void loop() {
  const CardReadResult r = g_reader.poll();
  if (r.status == ReadStatus::Ok) {
    onCard(r.number);
  } else if (r.status == ReadStatus::Unsupported) {
    onUnsupportedCard();
  }

  if (g_queue.expirePending(millis())) {
    g_display.render(g_queue, nullptr);
  }
  pollSkipButton();
}
