#include "CardReader.h"

#include <Adafruit_PN532.h>
#include <Arduino.h>
#include <Wire.h>
#include <string.h>

#include "TUnion.h"
#include "config.h"

namespace {

Adafruit_PN532 g_nfc(config::kPinNfcIrq, config::kPinNfcRst, &Wire);

/** 交通联合卡 SELECT 响应一般在 60 字节以内，留一倍余量给带额外 TLV 的发卡方。 */
constexpr size_t kRespMax = 128;
constexpr size_t kCmdMax = 32;

/** 10 字节 UID 的卡（部分城市卡、手机虚拟卡）很常见，按 7 字节开会越界。 */
constexpr size_t kUidMax = 10;

/** 连续多少次健康检查失败，才认定读卡器掉线。 */
constexpr uint8_t kRecoverGiveUp = 2;

}  // namespace

bool CardReader::begin() {
  Wire.begin(config::kPinNfcSda, config::kPinNfcScl);
  g_nfc.begin();
  if (g_nfc.getFirmwareVersion() == 0) return false;
  g_nfc.setPassiveActivationRetries(0x02);
  g_nfc.SAMConfig();
  recoverAttempts_ = 0;
  return true;
}

bool CardReader::recover() {
  Wire.end();
  delay(50);
  if (begin()) return true;
  ++recoverAttempts_;
  return false;
}

bool CardReader::exchange(const uint8_t* cmd, size_t cmdLen, uint8_t* resp, uint8_t* respLen) {
  if (cmdLen > kCmdMax) return false;
  uint8_t buf[kCmdMax];
  memcpy(buf, cmd, cmdLen);
  return g_nfc.inDataExchange(buf, static_cast<uint8_t>(cmdLen), resp, respLen);
}

bool CardReader::readCardNumber(uint8_t out[kCardNumberBytes]) {
  uint8_t resp[kRespMax];

  for (int attempt = 0; attempt < 2; ++attempt) {
    uint8_t len = sizeof(resp);
    if (exchange(tunion::kSelectWalletAid, tunion::kSelectWalletAidLen, resp, &len) &&
        tunion::isSuccess(resp, len)) {
      return tunion::parseCardNumber(resp, len, out);
    }
    if (attempt == 0) {
      uint8_t ppseLen = sizeof(resp);
      exchange(tunion::kSelectPpse, tunion::kSelectPpseLen, resp, &ppseLen);
    }
  }
  return false;
}

CardReadResult CardReader::poll() {
  CardReadResult result;
  result.status = ReadStatus::NoCard;
  memset(result.number, 0, sizeof(result.number));

  if (++pollCount_ % config::kReaderHealthCheckEvery == 0) {
    if (g_nfc.getFirmwareVersion() == 0) {
      // 第一次失败只是尝试重连；连续失败才上升为故障，避免偶发的总线抖动刷屏。
      if (!recover() && recoverAttempts_ >= kRecoverGiveUp) {
        result.status = ReadStatus::ReaderFault;
        return result;
      }
    } else {
      recoverAttempts_ = 0;
    }
  }

  uint8_t uid[kUidMax];
  uint8_t uidLen = 0;
  if (!g_nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen,
                                 config::kPollTimeoutMs)) {
    return result;
  }

  if (readCardNumber(result.number)) {
    result.status = ReadStatus::Ok;
  } else {
    result.status = ReadStatus::Unsupported;
  }
  return result;
}

bool CardReader::waitForRemoval(uint32_t maxMs) {
  uint8_t uid[kUidMax];
  uint8_t uidLen = 0;
  uint8_t misses = 0;
  const uint32_t start = millis();

  while (millis() - start < maxMs) {
    if (g_nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen,
                                  config::kPollTimeoutMs)) {
      misses = 0;
    } else if (++misses >= config::kRemovalMisses) {
      return true;
    }
  }
  return false;
}