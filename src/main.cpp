#include <Arduino.h>
#include <stdio.h>

#include "CardId.h"
#include "CardReader.h"
#include "QueueDisplay.h"
#include "QueueManager.h"
#include "QueueStorage.h"
#include "config.h"

namespace {

QueueManager g_queue(config::kConfirmWindowMs, config::kMachineCount);
QueueStorage g_storage;
QueueDisplay g_display;
CardReader g_reader;

uint64_t g_salt = 0;
uint64_t g_adminHash = 0;
uint32_t g_adminUnlockUntilMs = 0;
uint32_t g_lastClockRenderMs = 0;
bool g_readerFaultShown = false;

/** 队列变了之后落盘，失败就把原因摆到屏幕上，不再静默丢数据。 */
bool persist() {
  if (g_storage.save(g_queue)) return true;
  g_display.render(g_queue, "STORAGE ERROR");
  return false;
}

/**
 * 处理一次成功读到的交通联合卡：更新队列、保存、刷新屏幕，并等卡拿开。
 *
 * @param number 10 字节卡号。
 */
void onCard(const uint8_t number[kCardNumberBytes]) {
  const CardId id = makeCardId(number, g_salt);

  if (g_adminHash != 0 && id.hash == g_adminHash) {
    g_adminUnlockUntilMs = millis() + config::kAdminUnlockMs;
    g_display.render(g_queue, "STAFF MODE 60s");
    g_reader.waitForRemoval(config::kRemovalWaitMs);
    return;
  }

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
      snprintf(message, sizeof(message), "#%d TAP AGAIN TO LEAVE", r.position);
      break;
    case TapOutcome::Left:
      snprintf(message, sizeof(message), "LEFT #%d", r.position);
      break;
  }

  if (r.outcome == TapOutcome::Joined || r.outcome == TapOutcome::Left) {
    if (!persist()) {
      g_reader.waitForRemoval(config::kRemovalWaitMs);
      return;
    }
  }
  g_display.render(g_queue, message);
  g_reader.waitForRemoval(config::kRemovalWaitMs);
}

/** 处理读到卡但不是能读出卡号的交通联合卡：只提示，不改队列。 */
void onUnsupportedCard() {
  g_display.render(g_queue, "UNSUPPORTED CARD");
  g_reader.waitForRemoval(config::kRemovalWaitMs);
}

/** 读卡器掉线时的提示，只在进入故障状态时刷一次屏。 */
void onReaderFault() {
  if (!g_readerFaultShown) {
    g_display.showFatal("READER LOST");
    g_readerFaultShown = true;
  }
  delay(200);
}

/**
 * 等一次按钮按下，返回按住的时长（毫秒）。
 *
 * @param timeoutMs 多久之内没按下就返回 0。
 */
uint32_t waitForPress(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (digitalRead(config::kPinSkipButton) != LOW) {
    if (millis() - start >= timeoutMs) return 0;
    delay(10);
  }
  const uint32_t pressedAt = millis();
  while (digitalRead(config::kPinSkipButton) == LOW) {
    if (millis() - pressedAt >= config::kMaxHoldMs) break;
    delay(10);
  }
  return millis() - pressedAt;
}

/**
 * 开机时按钮被按住了多久（毫秒）。上电时没按着就直接返回 0，不拖慢开机。
 *
 * 用来区分“正常上电”和“按住按钮上电，要登记店员卡”。
 */
uint32_t holdAtBoot() {
  if (digitalRead(config::kPinSkipButton) != LOW) return 0;
  const uint32_t start = millis();
  while (digitalRead(config::kPinSkipButton) == LOW && millis() - start < config::kMaxHoldMs) {
    delay(10);
  }
  return millis() - start;
}

/** 等按钮松开，最多等 maxMs，用来避免一次长按被当成两次操作。 */
void waitForRelease(uint32_t maxMs) {
  const uint32_t start = millis();
  while (digitalRead(config::kPinSkipButton) == LOW && millis() - start < maxMs) {
    delay(10);
  }
}

/** 按钮权限是否还在有效期内。没设置店员卡时任何人都能用。 */
bool buttonUnlocked() {
  if (g_adminHash == 0) return true;
  return static_cast<int32_t>(g_adminUnlockUntilMs - millis()) > 0;
}

/**
 * 移人模式：长按选中的位置就移除，短按往下挪一格，一段时间不操作自动退出。
 *
 * 用来处理“排到中间的人走了”和“队首打完不走”两种现场问题。
 */
void kickMode() {
  if (g_queue.size() == 0) {
    g_display.render(g_queue, "QUEUE EMPTY");
    return;
  }

  size_t selected = 0;
  for (;;) {
    g_display.render(g_queue, "HOLD=REMOVE TAP=NEXT", static_cast<int>(selected));

    const uint32_t held = waitForPress(config::kKickModeIdleMs);
    if (held == 0) {
      g_display.render(g_queue, nullptr);
      return;
    }
    if (held < config::kSkipHoldMs) {
      selected = (selected + 1) % g_queue.size();
      continue;
    }

    char message[24];
    const int position = static_cast<int>(selected) + 1;
    if (g_queue.removeAt(selected)) {
      g_queue.refreshPlaySlots(millis());
      snprintf(message, sizeof(message), "REMOVED #%d", position);
      persist();
      g_display.render(g_queue, message);
    } else {
      g_display.render(g_queue, "REMOVE FAILED");
    }
    return;
  }
}

/** 检查按钮：长按进入移人模式，没有权限就提示刷店员卡。 */
void pollButton() {
  if (digitalRead(config::kPinSkipButton) != LOW) return;

  const uint32_t held = waitForPress(config::kSkipHoldMs);
  if (held < config::kSkipHoldMs) return;

  waitForRelease(config::kMaxHoldMs);

  if (!buttonUnlocked()) {
    g_display.render(g_queue, "TAP STAFF CARD FIRST");
    return;
  }
  kickMode();
}

/**
 * 开机时按住按钮，可以把下一张刷的卡登记成店员卡。
 *
 * 设置过之后，长按移人要先刷店员卡；想换卡就再走一次这个流程。
 */
void offerStaffProvisioning() {
  if (holdAtBoot() < config::kSkipHoldMs) return;

  g_display.render(g_queue, "TAP STAFF CARD");
  const uint32_t start = millis();
  while (millis() - start < config::kProvisionTimeoutMs) {
    const CardReadResult r = g_reader.poll();
    if (r.status == ReadStatus::Ok) {
      const CardId id = makeCardId(r.number, g_salt);
      if (g_storage.saveAdminHash(id.hash)) {
        g_adminHash = id.hash;
        g_adminUnlockUntilMs = millis() + config::kAdminUnlockMs;
        g_display.render(g_queue, "STAFF CARD SET");
      } else {
        g_display.render(g_queue, "STORAGE ERROR");
      }
      g_reader.waitForRemoval(config::kRemovalWaitMs);
      return;
    }
    if (r.status == ReadStatus::ReaderFault) break;
    delay(50);
  }
  g_display.render(g_queue, nullptr);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(config::kCpuMhz);
  pinMode(config::kPinSkipButton, INPUT_PULLUP);

  g_storage.begin();
  g_salt = g_storage.loadOrCreateSalt();
  g_adminHash = g_storage.loadAdminHash();

  const bool restored = g_storage.load(g_queue);
  g_queue.claimStartupTimestamps(millis());
  if (restored) Serial.printf("queue restored: %u\n", static_cast<unsigned>(g_queue.size()));

  g_display.begin();
  if (!g_reader.begin()) {
    g_display.showFatal("PN532 NOT FOUND");
    while (true) delay(1000);
  }
  g_display.render(g_queue, nullptr);

  offerStaffProvisioning();
}

void loop() {
  const CardReadResult r = g_reader.poll();
  if (r.status == ReadStatus::Ok) {
    g_readerFaultShown = false;
    onCard(r.number);
  } else if (r.status == ReadStatus::Unsupported) {
    onUnsupportedCard();
  } else if (r.status == ReadStatus::ReaderFault) {
    onReaderFault();
  }

  if (g_queue.expirePending(millis())) {
    g_display.render(g_queue, nullptr);
  }

  const size_t stale = g_queue.expireStale(millis(), config::kStaleEntryMs);
  if (stale > 0) {
    Serial.printf("dropped %u stale entries\n", static_cast<unsigned>(stale));
    persist();
    g_display.render(g_queue, "STALE DROPPED");
  }

  // 上机时长每分钟刷一次。平时靠“内容有变化才刷新”省电，时间戳不会自己走动，
  // 所以这里主动补一跳，否则队首打了多久会一直停在进入上机位那一刻。
  if (millis() - g_lastClockRenderMs >= config::kClockRefreshMs) {
    g_lastClockRenderMs = millis();
    for (size_t i = 0; i < g_queue.size(); ++i) {
      if (g_queue.isPlaying(i)) {
        g_display.render(g_queue, nullptr);
        break;
      }
    }
  }

  pollButton();
}