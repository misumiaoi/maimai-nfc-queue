#include <Arduino.h>
#include <stdio.h>

#include "CardId.h"
#include "CardReader.h"
#include "QueueDisplay.h"
#include "QueueManager.h"
#include "QueueStorage.h"
#include "TapGate.h"
#include "config.h"

namespace {

QueueManager g_queue(config::kConfirmWindowMs);
QueueStorage g_storage;
QueueDisplay g_display;
CardReader g_reader;
TapGate g_gate(config::kRemovalMisses, config::kSameCardCooldownMs);
uint64_t g_salt = 0;

/**
 * 处理一次刷卡：更新队列、保存、刷新屏幕。
 *
 * @param id 刷卡的身份。
 */
void onCard(const CardId& id) {
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

  // 墨水屏刷新要 1 到 4 秒，确认窗口从提示真正显示出来时开始算。
  if (r.outcome == TapOutcome::ConfirmLeave) {
    g_queue.restartPendingTimer(millis());
  }
}

/** 处理一次轮询结果。处理过的卡在拿开之前不会再被处理。 */
void onRead(const CardReadResult& r) {
  CardId id{};
  switch (r.status) {
    case ReadStatus::NoCard:
      return;
    case ReadStatus::Unsupported:
      g_display.render(g_queue, "UNSUPPORTED CARD");
      g_gate.onIgnored();
      return;
    case ReadStatus::Transit:
      id = makeCardId(r.number, g_salt);
      break;
    case ReadStatus::FixedUid:
      id = makeUidCardId(r.uid, r.uidLen, g_salt);
      break;
  }

  if (!g_gate.accepts(id.hash, millis())) {
    g_gate.onIgnored();
    return;
  }
  onCard(id);
  g_gate.onHandled(id.hash);
}

/**
 * 检查管理按钮。按住 config::kSkipHoldMs 后松开移除队首，
 * 按住 config::kClearHoldMs 后松开清空队列。到达每个时长时屏幕会提示松开后的效果。
 *
 * 按下后会阻塞到松开，松开前读卡器不工作。
 */
void pollButton() {
  if (digitalRead(config::kPinSkipButton) != LOW) return;

  enum class Action { None, Skip, Clear };
  Action action = Action::None;
  const uint32_t start = millis();

  while (digitalRead(config::kPinSkipButton) == LOW) {
    const uint32_t held = millis() - start;
    if (action != Action::Clear && held >= config::kClearHoldMs) {
      action = Action::Clear;
      if (g_queue.size() > 0) g_display.render(g_queue, "RELEASE: CLEAR ALL");
    } else if (action == Action::None && held >= config::kSkipHoldMs) {
      action = Action::Skip;
      if (g_queue.size() > 0) g_display.render(g_queue, "RELEASE: REMOVE #1");
    }
    delay(20);
  }

  bool changed = false;
  const char* message = nullptr;
  if (action == Action::Clear) {
    changed = g_queue.clear();
    message = "QUEUE CLEARED";
  } else if (action == Action::Skip) {
    changed = g_queue.removeHead();
    message = "HEAD REMOVED";
  }
  if (changed) {
    g_storage.save(g_queue);
    g_display.render(g_queue, message);
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
  if (g_gate.waitingForRemoval()) {
    g_gate.observe(g_reader.isCardPresent(), millis());
  } else {
    onRead(g_reader.poll());
  }

  if (g_queue.expirePending(millis())) {
    g_display.render(g_queue, nullptr);
  }
  pollButton();
}
