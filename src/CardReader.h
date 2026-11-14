#pragma once

#include <stdint.h>

#include "CardId.h"

/** 一次轮询的状态。 */
enum class ReadStatus {
  NoCard,       ///< 无新刷卡事件（没有卡，或卡尚未移开）。
  Removed,      ///< 上次处理的卡已连续多次检测不到，可以再次刷卡。
  Ok,           ///< 读到交通联合卡，number 有效。
  Unsupported,  ///< 有卡，但不是能读出卡号的交通联合卡（门禁卡、银行卡、其他城市卡等）。
};

/** CardReader::poll 的返回值。 */
struct CardReadResult {
  ReadStatus status;
  uint8_t number[kCardNumberBytes];  ///< status == Ok 时有效。
};

/**
 * PN532 读卡器封装（I2C），只读交通联合卡的公开卡号。
 *
 * 不使用芯片 UID，因为手机里的虚拟卡 UID 可能每次都变。卡号来自应用层的
 * SELECT 响应（见 TUnion.h），不读余额和交易记录，也不写卡。
 */
class CardReader {
 public:
  /**
   * 初始化 I2C 和 PN532。
   *
   * @return 读到 PN532 固件版本返回 true；接线或拨码开关不对时返回 false。
   */
  bool begin();

  /**
   * 轮询一次。没有卡时最多阻塞 config::kPollTimeoutMs。
   *
   * 读到卡后依次尝试 SELECT 钱包应用；失败则先 SELECT PPSE 再重试一次，
   * 以兼容不接受直接选 AID 的卡。同一次持续贴卡只处理一次；卡未移开时返回 NoCard。
   */
  CardReadResult poll();

 private:
  /** 发送一条 APDU 并取回响应，成功指通信成功，状态字由调用方检查。 */
  bool exchange(const uint8_t* cmd, size_t cmdLen, uint8_t* resp, uint8_t* respLen);

  /** 在已激活的卡上读出卡号。 */
  bool readCardNumber(uint8_t out[kCardNumberBytes]);
};
