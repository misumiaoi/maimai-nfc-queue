#pragma once

#include <stddef.h>
#include <stdint.h>

/** 交通联合卡卡号的字节数（BCD，共 20 位数字）。 */
constexpr size_t kCardNumberBytes = 10;

/** ISO14443A UID 的最大字节数（三重 UID）。 */
constexpr size_t kMaxUidBytes = 10;

/** 为屏幕显示保留的卡号末尾位数。默认只显示 4 位，重号时可以显示到 6 位。 */
constexpr size_t kTailDigits = 6;

/**
 * 一张卡在本系统里的身份。
 *
 * 队列只保存这个结构，不保存完整卡号或 UID：
 *  - hash：卡号（或固定 UID）加本机随机盐后的 64 位散列，用来判断“是不是同一张卡”。
 *  - tail：末尾 6 位，用来在屏幕上显示。交通联合卡是卡号末 6 位数字，
 *          按 UID 识别的卡是 UID 末 3 字节的十六进制。
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
 * 散列用的是 64 位 FNV-1a，不是密码学散列。队列只在本机保存，目的只是不直接存卡号。
 */
CardId makeCardId(const uint8_t number[kCardNumberBytes], uint64_t salt);

/**
 * 由固定的 ISO14443A UID 生成 CardId，用于读不出交通联合卡号的实体卡（例如 aime 卡）。
 *
 * 散列输入在盐之后先放一个 0xA0 标记字节和 UID 长度。BCD 卡号的每个字节都不超过 0x99，
 * 所以 UID 的散列输入不可能和某个卡号的散列输入相同。
 *
 * @param uid    UID 字节。
 * @param len    UID 长度，1 到 kMaxUidBytes。
 * @param salt   本机随机盐。
 */
CardId makeUidCardId(const uint8_t* uid, size_t len, uint64_t salt);

/**
 * 判断 UID 是否每次都相同，能不能拿来当身份。
 *
 * 7 字节和 10 字节 UID 是出厂固定的。4 字节 UID 如果首字节是 0x08，按 ISO14443-3
 * 是每次随机生成的（手机模拟卡大多如此），不能用；其他 4 字节 UID 视为固定。
 */
bool isFixedUid(const uint8_t* uid, size_t len);
