#pragma once

#include <stddef.h>
#include <stdint.h>

/** 交通联合卡卡号的字节数（BCD，共 20 位数字）。 */
constexpr size_t kCardNumberBytes = 10;

/**
 * 为屏幕显示保留的卡号末尾位数。
 *
 * 默认只显示 4 位；重号时逐位加长，最多到 8 位。10 字节 BCD 一共 20 位数字，
 * 取 8 位不会越界。真要在 8 位上都撞，说明两张卡的前 12 位也基本一样，
 * 属于发卡方的重号问题，这里不做兜底。
 */
constexpr size_t kTailDigits = 8;

/**
 * 一张卡在本系统里的身份。
 *
 * 队列只保存这个结构，不保存完整卡号：
 *  - hash：卡号加本机随机盐后的 64 位散列，用来判断“是不是同一张卡”。
 *  - tail：卡号末尾 8 位数字，用来在屏幕上显示。
 */
struct CardId {
  uint64_t hash;
  char tail[kTailDigits + 1];
};

/**
 * 由 10 字节 BCD 卡号生成 CardId。
 *
 * @param number 交通联合卡的 10 字节卡号（BCD）。
 * @param salt   本机随机盐。不同设备用不同盐，避免散列值在设备之间可比对。
 * @return 填好 hash 和 tail 的 CardId。
 *
 * 散列用的是 64 位 FNV-1a，不是密码学散列。它只能保证“不存明文卡号”，
 * 不能保证拿到盐之后无法反推：卡号本身有固定结构（发卡方标识、城市码），
 * 枚举空间并不大。需要更强的话换成 SipHash，但那样就得引第三方的库。
 */
CardId makeCardId(const uint8_t number[kCardNumberBytes], uint64_t salt);