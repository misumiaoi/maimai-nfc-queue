#pragma once

#include <stdint.h>

/**
 * 有源蜂鸣器。
 *
 * 墨水屏刷新要一两秒，鸣叫在刷新之前发生，用来立刻表示刷卡、按钮步骤和错误。
 * 接 config::kPinBuzzer，高电平鸣叫。这是电平驱动，不是需要 PWM 的无源蜂鸣器。
 */
class Buzzer {
 public:
  /** 把引脚设为输出并保持安静。 */
  void begin();

  /**
   * 连续短鸣 count 声。
   *
   * @param count 鸣叫次数。0 表示不响。会阻塞到这一串结束。
   */
  void beep(uint8_t count);
};
