#include "CardReader.h"

#include <Adafruit_PN532.h>
#include <Arduino.h>
#include <Wire.h>
#include <string.h>

#include "TUnion.h"
#include "CardPresence.h"
#include "config.h"

namespace {

Adafruit_PN532 g_nfc(config::kPinNfcIrq, config::kPinNfcRst, &Wire);
CardPresence g_presence(config::kRemovalMisses);

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
      len = sizeof(resp);
      exchange(tunion::kSelectPpse, tunion::kSelectPpseLen, resp, &len);
    }
  }
  return false;
}

CardReadResult CardReader::poll() {
  CardReadResult result;
  result.status = ReadStatus::NoCard;
  memset(result.number, 0, sizeof(result.number));

  uint8_t uid[7];
  uint8_t uidLen = 0;
  const bool wasWaiting = g_presence.waitingForRemoval();
  const bool present = g_nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen,
                                                config::kPollTimeoutMs);
  const bool accepted = g_presence.observe(present);
  if (wasWaiting && !g_presence.waitingForRemoval()) result.status = ReadStatus::Removed;
  if (!accepted) return result;

  if (readCardNumber(result.number)) {
    result.status = ReadStatus::Ok;
  } else {
    result.status = ReadStatus::Unsupported;
  }
  return result;
}
