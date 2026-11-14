#include "CardId.h"

namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

/** FNV-1a 单字节步进。 */
uint64_t fnvStep(uint64_t h, uint8_t byte) {
  h ^= byte;
  h *= kFnvPrime;
  return h;
}

}  // namespace

CardId makeCardId(const uint8_t number[kCardNumberBytes], uint64_t salt) {
  CardId id;

  uint64_t h = kFnvOffset;
  for (int i = 0; i < 8; ++i) {
    h = fnvStep(h, static_cast<uint8_t>(salt >> (8 * i)));
  }
  for (size_t i = 0; i < kCardNumberBytes; ++i) {
    h = fnvStep(h, number[i]);
  }
  id.hash = h;

  // BCD 每个半字节就是一位十进制数字，所以十六进制字符就是卡号数字。
  static const char kHex[] = "0123456789ABCDEF";
  const size_t tailBytes = kTailDigits / 2;
  for (size_t i = 0; i < tailBytes; ++i) {
    uint8_t b = number[kCardNumberBytes - tailBytes + i];
    id.tail[2 * i] = kHex[b >> 4];
    id.tail[2 * i + 1] = kHex[b & 0x0F];
  }
  id.tail[kTailDigits] = '\0';
  return id;
}