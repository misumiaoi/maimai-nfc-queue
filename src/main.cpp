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
bool g_ready = false;

bool saveOrStop(const QueueManager& previous) {
  if (g_storage.save(g_queue)) return true;
  g_queue = previous;
  g_ready = false;
  g_display.showFatal("STORAGE ERROR");
  return false;
}

/**
 * 处理一次新的贴卡：更新队列、保存、刷新屏幕。
 *
 * @param number 10 字节卡号。
 */
void onCard(const uint8_t number[kCardNumberBytes]) {
  const CardId id = makeCardId(number, g_salt);
  const QueueManager previous = g_queue;
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
      g_queue.pausePending();
      snprintf(message, sizeof(message), "#%d: LIFT, TAP TO LEAVE", r.position);
      break;
    case TapOutcome::Left:
      snprintf(message, sizeof(message), "LEFT #%d", r.position);
      break;
  }

  if (r.outcome == TapOutcome::Joined || r.outcome == TapOutcome::Left) {
    if (!saveOrStop(previous)) return;
  }
  g_display.render(g_queue, message);
}

/** 处理读到卡但不是交通联合卡的情况：只提示，不改队列。 */
void onUnsupportedCard() {
  g_display.render(g_queue, "UNSUPPORTED CARD");
}

/**
 * 检查跳过按钮，长按达到 config::kSkipHoldMs 才移除队首，避免误触。
 *
 * 非阻塞；一次长按只移除一次，松开后才能再次触发。
 */
void pollSkipButton() {
  static bool held = false;
  static bool fired = false;
  static uint32_t start = 0;
  if (digitalRead(config::kPinSkipButton) != LOW) {
    held = false;
    fired = false;
    return;
  }
  if (!held) {
    held = true;
    start = millis();
  }
  if (!fired && millis() - start >= config::kSkipHoldMs) {
    fired = true;
    const QueueManager previous = g_queue;
    if (g_queue.removeHead() && saveOrStop(previous)) {
      g_display.render(g_queue, "HEAD REMOVED");
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(config::kCpuMhz);
  pinMode(config::kPinSkipButton, INPUT_PULLUP);

  g_display.begin();
  if (!g_storage.begin() || !g_storage.loadOrCreateSalt(g_salt) || !g_storage.load(g_queue)) {
    g_display.showFatal("STORAGE ERROR");
    return;
  }
  if (!g_reader.begin()) {
    g_display.showFatal("PN532 NOT FOUND");
    return;
  }
  g_display.render(g_queue, nullptr);
  g_ready = true;
}

void loop() {
  if (!g_ready) {
    delay(100);
    return;
  }
  const CardReadResult r = g_reader.poll();
  if (r.status == ReadStatus::Ok) {
    onCard(r.number);
  } else if (r.status == ReadStatus::Unsupported) {
    onUnsupportedCard();
  } else if (r.status == ReadStatus::Removed) {
    g_queue.resumePending(millis());
  }

  if (!g_ready) return;
  if (g_queue.expirePending(millis())) {
    g_display.render(g_queue, nullptr);
  }
  pollSkipButton();
}
