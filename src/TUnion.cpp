#include "TUnion.h"

#include <string.h>

namespace tunion {

const uint8_t kSelectPpse[] = {0x00, 0xA4, 0x04, 0x00, 0x0E, '2',  'P',  'A',  'Y',  '.',
                               'S',  'Y',  'S',  '.',  'D',  'D',  'F',  '0',  '1',  0x00};
const size_t kSelectPpseLen = sizeof(kSelectPpse);

const uint8_t kSelectWalletAid[] = {0x00, 0xA4, 0x04, 0x00, 0x08, 0xA0, 0x00,
                                    0x00, 0x06, 0x32, 0x01, 0x01, 0x05, 0x00};
const size_t kSelectWalletAidLen = sizeof(kSelectWalletAid);

namespace {

constexpr uint32_t kTagCardInfo = 0x9F0C;
constexpr size_t kCardInfoMinLen = 20;
constexpr size_t kCardNumberOffset = 10;
constexpr int kMaxDepth = 4;

/**
 * 解析一个 BER 标签。
 *
 * @param p      当前位置。
 * @param end    数据末尾。
 * @param tag    输出：标签值（1 到 3 字节拼成整数）。
 * @param isCons 输出：是否为构造型标签（内部还嵌套 TLV）。
 * @return 标签占用的字节数，格式错误返回 0。
 */
size_t readTag(const uint8_t* p, const uint8_t* end, uint32_t* tag, bool* isCons) {
  if (p >= end) return 0;
  size_t n = 1;
  uint32_t t = p[0];
  *isCons = (p[0] & 0x20) != 0;
  if ((p[0] & 0x1F) == 0x1F) {
    do {
      if (p + n >= end || n >= 3) return 0;
      t = (t << 8) | p[n];
      ++n;
    } while (p[n - 1] & 0x80);
  }
  *tag = t;
  return n;
}

/**
 * 解析 BER 长度字段，支持短格式和 0x81 / 0x82 长格式。
 *
 * @return 长度字段占用的字节数，格式错误返回 0。
 */
size_t readLength(const uint8_t* p, const uint8_t* end, size_t* length) {
  if (p >= end) return 0;
  if (p[0] < 0x80) {
    *length = p[0];
    return 1;
  }
  if (p[0] == 0x81 && p + 1 < end) {
    *length = p[1];
    return 2;
  }
  if (p[0] == 0x82 && p + 2 < end) {
    *length = (static_cast<size_t>(p[1]) << 8) | p[2];
    return 3;
  }
  return 0;
}

/**
 * 在 TLV 数据里递归查找某个标签，会进入构造型标签（例如 6F、A5）。
 *
 * @param data  待搜索的 TLV 数据。
 * @param len   数据长度。
 * @param want  要找的标签。
 * @param value 输出：找到的值的起始地址。
 * @param vlen  输出：值的长度。
 * @param depth 当前递归深度，用来限制嵌套层数。
 */
bool findTag(const uint8_t* data, size_t len, uint32_t want, const uint8_t** value,
             size_t* vlen, int depth) {
  if (depth > kMaxDepth) return false;
  const uint8_t* p = data;
  const uint8_t* end = data + len;
  while (p < end) {
    uint32_t tag = 0;
    bool cons = false;
    size_t tn = readTag(p, end, &tag, &cons);
    if (tn == 0) return false;
    p += tn;

    size_t l = 0;
    size_t ln = readLength(p, end, &l);
    if (ln == 0) return false;
    p += ln;
    if (l > static_cast<size_t>(end - p)) return false;

    if (tag == want) {
      *value = p;
      *vlen = l;
      return true;
    }
    if (cons && findTag(p, l, want, value, vlen, depth + 1)) return true;
    p += l;
  }
  return false;
}

}  // namespace

bool isSuccess(const uint8_t* resp, size_t len) {
  return len >= 2 && resp[len - 2] == 0x90 && resp[len - 1] == 0x00;
}

bool parseCardNumber(const uint8_t* resp, size_t len, uint8_t out[kCardNumberBytes]) {
  if (!isSuccess(resp, len)) return false;

  const uint8_t* info = nullptr;
  size_t infoLen = 0;
  if (!findTag(resp, len - 2, kTagCardInfo, &info, &infoLen, 0)) return false;
  if (infoLen < kCardInfoMinLen) return false;

  const uint8_t* num = info + kCardNumberOffset;
  bool allZero = true;
  bool allFf = true;
  for (size_t i = 0; i < kCardNumberBytes; ++i) {
    if (num[i] != 0x00) allZero = false;
    if (num[i] != 0xFF) allFf = false;
  }
  if (allZero || allFf) return false;

  memcpy(out, num, kCardNumberBytes);
  return true;
}

}  // namespace tunion
