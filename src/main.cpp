#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "CardId.h"
#include "CardReader.h"
#include "NamePortal.h"
#include "NameRegistry.h"
#include "QueueDisplay.h"
#include "QueueManager.h"
#include "QueueStorage.h"
#include "Registrar.h"
#include "config.h"

namespace {

QueueManager g_queue(config::kConfirmWindowMs);
NameRegistry g_names;
Registrar g_registrar(config::kBindWindowMs);
QueueStorage g_storage;
QueueDisplay g_display;
CardReader g_reader;
NamePortal g_portal;
uint64_t g_salt = 0;

bool g_showBind = false;
bool g_refreshQueue = false;

char g_line[48];
size_t g_lineLen = 0;
bool g_lineOverflow = false;

/** 按当前等待状态刷新屏幕。正在等贴卡时，队列画面让位给绑定提示。 */
void showState(const char* message) {
  if (g_registrar.armed()) {
    g_display.showBindPrompt(g_registrar.pending());
    return;
  }
  g_display.render(g_queue, g_names, message);
}

/**
 * 名字已经到了，开始等下一次贴卡。屏幕刷新放到 loop 里，避免堵在网页响应里。
 *
 * @param name 规范化后的显示名。
 */
void armFromInput(const char* name) {
  if (!g_registrar.arm(name, millis())) {
    Serial.println("ERR bad name");
    return;
  }
  g_showBind = true;
  g_refreshQueue = false;
  Serial.print("OK tap card to bind ");
  Serial.println(name);
}

/** 取消等待中的登记，下一次刷卡恢复成入队或离队。 */
void cancelInput() {
  g_registrar.cancel();
  g_showBind = false;
  g_refreshQueue = true;
  Serial.println("OK cancelled");
}

/** 把串口的一行交给登记解析。 */
void handleLine(const char* line) {
  const LineCommand cmd = parseNameLine(line);
  if (cmd.kind == LineKind::Empty) return;
  if (cmd.kind == LineKind::Cancel) {
    cancelInput();
    return;
  }
  if (cmd.kind == LineKind::Bind) {
    armFromInput(cmd.name);
    return;
  }
  Serial.println("ERR use: NAME <name> | CANCEL");
}

/** 非阻塞地读串口。一行以换行结束。 */
void pollSerial() {
  while (Serial.available() > 0) {
    const int raw = Serial.read();
    if (raw < 0) break;
    const char c = static_cast<char>(raw);
    if (c == '\r') continue;
    if (c != '\n') {
      if (g_lineLen + 1 < sizeof(g_line)) {
        g_line[g_lineLen++] = c;
      } else {
        g_lineOverflow = true;
      }
      continue;
    }
    g_line[g_lineLen] = '\0';
    if (g_lineOverflow) {
      Serial.println("ERR use: NAME <name> | CANCEL");
    } else {
      handleLine(g_line);
    }
    g_lineLen = 0;
    g_lineOverflow = false;
  }
}

/** 网页或串口请求的画面更新。墨水屏刷新比较慢，不放在网页回调里做。 */
void flushUi() {
  if (!g_showBind && !g_refreshQueue) return;
  const bool bind = g_showBind && g_registrar.armed();
  g_showBind = false;
  g_refreshQueue = false;
  if (bind) {
    g_display.showBindPrompt(g_registrar.pending());
  } else {
    g_display.render(g_queue, g_names, nullptr);
  }
}

/**
 * 处理一次成功读到的交通联合卡。
 *
 * 若正在等登记，这一下只把名字绑到 salted hash 上，不入队也不离队。
 * 完整卡号只存在这次调用的参数里，散列之后不再使用。
 *
 * @param number 10 字节卡号。
 */
void onCard(const uint8_t number[kCardNumberBytes]) {
  const CardId id = makeCardId(number, g_salt);
  const uint32_t now = millis();
  const Registrar::BindTap bind = g_registrar.consume(id.hash, g_names, now);

  char message[32];
  const char* text = message;
  bool saveNames = false;

  if (bind == Registrar::BindTap::Bound) {
    saveNames = true;
    const char* name = g_names.find(id.hash);
    snprintf(message, sizeof(message), "BOUND %s", name != nullptr ? name : "");
  } else if (bind == Registrar::BindTap::Full) {
    text = "NAME LIST FULL";
  } else if (bind == Registrar::BindTap::Expired) {
    text = "BIND EXPIRED";
  } else {
    const TapResult r = g_queue.handleTap(id, now);
    switch (r.outcome) {
      case TapOutcome::Joined:
        snprintf(message, sizeof(message), "JOINED #%d", r.position);
        break;
      case TapOutcome::QueueFull:
        text = "QUEUE FULL";
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
  }

  if (saveNames && !g_storage.save(g_names)) text = "SAVE FAILED";
  g_showBind = false;
  g_refreshQueue = false;
  g_display.render(g_queue, g_names, text);
  g_reader.waitForRemoval(config::kRemovalWaitMs);
}

/** 处理读到卡但不是交通联合卡的情况：只提示，不改队列，也不绑定。 */
void onUnsupportedCard() {
  g_display.render(g_queue, g_names, "UNSUPPORTED CARD");
  g_reader.waitForRemoval(config::kRemovalWaitMs);
  if (g_registrar.expire(millis())) {
    g_display.render(g_queue, g_names, "BIND EXPIRED");
  } else if (g_registrar.armed()) {
    g_display.showBindPrompt(g_registrar.pending());
  }
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
        showState("HEAD REMOVED");
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
  g_storage.load(g_names);

  g_display.begin();
  if (!g_reader.begin()) {
    g_display.showFatal("PN532 NOT FOUND");
    while (true) delay(1000);
  }
  g_portal.begin(armFromInput, cancelInput);
  Serial.println("serial: NAME <name> | CANCEL");
  g_display.render(g_queue, g_names, nullptr);
}

void loop() {
  g_portal.handle();
  pollSerial();
  flushUi();

  const CardReadResult r = g_reader.poll();
  if (r.status == ReadStatus::Ok) {
    onCard(r.number);
  } else if (r.status == ReadStatus::Unsupported) {
    onUnsupportedCard();
  }

  const uint32_t now = millis();
  const bool bindExpired = g_registrar.expire(now);
  const bool leaveExpired = g_queue.expirePending(now);
  if (bindExpired) {
    g_display.render(g_queue, g_names, "BIND EXPIRED");
  } else if (leaveExpired && !g_registrar.armed()) {
    g_display.render(g_queue, g_names, nullptr);
  }
  pollSkipButton();
}
