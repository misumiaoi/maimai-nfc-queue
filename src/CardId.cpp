#include "CardId.h"

namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

/** UID 散列的标记字节，比任何 BCD 字节（最大 0x99）都大。 */
constexpr uint8_t kUidDomain = 0xA0;

/** 4 字节随机 UID 的首字节（ISO14443-3）。 */
constexpr uint8_t kRandomUidPrefix = 0x08;

const char kHex[] = "0123456789ABCDEF";

/** FNV-1a 单字节步进。 */
uint64_t fnvStep(uint64_t h, uint8_t byte) {
  h ^= byte;
  h *= kFnvPrime;
  return h;
}

/** 把盐按小端字节序喂进散列。 */
uint64_t hashSalt(uint64_t salt) {
  uint64_t h = kFnvOffset;
  for (int i = 0; i < 8; ++i) {
    h = fnvStep(h, static_cast<uint8_t>(salt >> (8 * i)));
  }
  return h;
}

/** 把 bytes 的最后 kTailDigits / 2 个字节写成十六进制字符串，长度不够时前面补 0。 */
void writeTail(const uint8_t* bytes, size_t len, char out[kTailDigits + 1]) {
  const size_t tailBytes = kTailDigits / 2;
  for (size_t i = 0; i < tailBytes; ++i) {
    uint8_t b = 0;
    if (len + i >= tailBytes) b = bytes[len + i - tailBytes];
    out[2 * i] = kHex[b >> 4];
    out[2 * i + 1] = kHex[b & 0x0F];
  }
  out[kTailDigits] = '\0';
}

}  // namespace

CardId makeCardId(const uint8_t number[kCardNumberBytes], uint64_t salt) {
  CardId id;
  uint64_t h = hashSalt(salt);
  for (size_t i = 0; i < kCardNumberBytes; ++i) {
    h = fnvStep(h, number[i]);
  }
  id.hash = h;
  // BCD 每个半字节就是一位十进制数字，所以十六进制字符就是卡号数字。
  writeTail(number, kCardNumberBytes, id.tail);
  return id;
}

CardId makeUidCardId(const uint8_t* uid, size_t len, uint64_t salt) {
  CardId id;
  uint64_t h = hashSalt(salt);
  h = fnvStep(h, kUidDomain);
  h = fnvStep(h, static_cast<uint8_t>(len));
  for (size_t i = 0; i < len; ++i) {
    h = fnvStep(h, uid[i]);
  }
  id.hash = h;
  writeTail(uid, len, id.tail);
  return id;
}

bool isFixedUid(const uint8_t* uid, size_t len) {
  if (len == 7 || len == 10) return true;
  if (len == 4) return uid[0] != kRandomUidPrefix;
  return false;
}
