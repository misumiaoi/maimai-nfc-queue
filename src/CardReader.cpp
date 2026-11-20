#include "CardReader.h"

#include <Adafruit_PN532.h>
#include <Arduino.h>
#include <Wire.h>
#include <string.h>

#include "TUnion.h"
#include "config.h"

namespace {

Adafruit_PN532 g_nfc(config::kPinNfcIrq, config::kPinNfcRst, &Wire);

constexpr size_t kRespMax = 96;
constexpr size_t kCmdMax = 32;

}  // namespace

bool CardReader::begin() {
  Wire.begin(config::kPinNfcSda, config::kPinNfcScl);
  g_nfc.begin();
  if (g_nfc.getFirmwareVersion() == 0) return false;
  g_nfc.setPassiveActivationRetries(0x02);
  g_nfc.SAMConfig();
  return true;
}

bool CardReader::detect(uint8_t uid[kMaxUidBytes], uint8_t* uidLen) {
  // 缓冲区按最长的 10 字节 UID 分配：库在拷贝 UID 时不检查长度。
  *uidLen = 0;
  if (!g_nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLen, config::kPollTimeoutMs)) {
    return false;
  }
  return *uidLen > 0 && *uidLen <= kMaxUidBytes;
}

bool CardReader::exchange(const uint8_t* cmd, size_t cmdLen, uint8_t* resp, uint8_t* respLen) {
  if (cmdLen > kCmdMax) return false;
  uint8_t buf[kCmdMax];
  memcpy(buf, cmd, cmdLen);
  return g_nfc.inDataExchange(buf, static_cast<uint8_t>(cmdLen), resp, respLen);
}

CardReader::NumberRead CardReader::readCardNumber(uint8_t out[kCardNumberBytes]) {
  uint8_t resp[kRespMax];

  for (int attempt = 0; attempt < 2; ++attempt) {
    uint8_t len = sizeof(resp);
    if (!exchange(tunion::kSelectWalletAid, tunion::kSelectWalletAidLen, resp, &len)) {
      return NumberRead::NoAnswer;
    }
    if (tunion::isSuccess(resp, len)) {
      return tunion::parseCardNumber(resp, len, out) ? NumberRead::Ok : NumberRead::Rejected;
    }
    if (attempt == 0) {
      len = sizeof(resp);
      if (!exchange(tunion::kSelectPpse, tunion::kSelectPpseLen, resp, &len)) {
        return NumberRead::NoAnswer;
      }
    }
  }
  return NumberRead::Rejected;
}

CardReadResult CardReader::poll() {
  CardReadResult result;
  result.status = ReadStatus::NoCard;
  memset(result.number, 0, sizeof(result.number));
  memset(result.uid, 0, sizeof(result.uid));
  result.uidLen = 0;

  if (!detect(result.uid, &result.uidLen)) return result;

  NumberRead read = readCardNumber(result.number);
  if (read == NumberRead::NoAnswer) {
    // 可能是不支持 APDU 的卡，也可能是读到一半卡移开了。重新寻卡确认是同一张卡再试一次。
    uint8_t uid[kMaxUidBytes];
    uint8_t uidLen = 0;
    if (!detect(uid, &uidLen) || uidLen != result.uidLen ||
        memcmp(uid, result.uid, uidLen) != 0) {
      return result;  // 卡已经拿走或换了一张，当作没读到。
    }
    read = readCardNumber(result.number);
  }

  if (read == NumberRead::Ok) {
    result.status = ReadStatus::Transit;
  } else if (isFixedUid(result.uid, result.uidLen)) {
    result.status = ReadStatus::FixedUid;
  } else {
    result.status = ReadStatus::Unsupported;
  }
  return result;
}

bool CardReader::isCardPresent() {
  uint8_t uid[kMaxUidBytes];
  uint8_t uidLen = 0;
  return detect(uid, &uidLen);
}
