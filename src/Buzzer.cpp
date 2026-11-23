#include "Buzzer.h"

#include <Arduino.h>

#include "config.h"

void Buzzer::begin() {
  // 先写入低电平再切换成输出，避免引脚从浮空变成输出时先响一声。
  digitalWrite(config::kPinBuzzer, LOW);
  pinMode(config::kPinBuzzer, OUTPUT);
}

void Buzzer::beep(uint8_t count) {
  for (uint8_t i = 0; i < count; ++i) {
    digitalWrite(config::kPinBuzzer, HIGH);
    delay(config::kBeepOnMs);
    digitalWrite(config::kPinBuzzer, LOW);
    if (i + 1 < count) delay(config::kBeepGapMs);
  }
}
