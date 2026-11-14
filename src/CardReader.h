#pragma once

#include <stdint.h>

#include "CardId.h"

/** 一次轮询的状态。 */
enum class ReadStatus {
  NoCard,       ///< 读卡器范围内没有卡。
  Ok,           ///< 读到交通联合卡，number 有效。
  Unsupported,  ///< 有卡，但不是能读出卡号的交通联合卡（门禁卡、银行卡等）。
  ReaderFault,  ///< 读卡器本身掉线，自动重连也失败。
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
 *
 * 每 config::kReaderHealthCheckEvery 次轮询做一次固件版本检查，读不到就重启
 * I2C 和芯片；连续失败后返回 ReaderFault，让上层把“读卡器掉了”显示出来，
 * 而不是安静地停在最后一帧。
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
   * 以兼容不接受直接选 AID 的卡。
   */
  CardReadResult poll();

  /**
   * 阻塞等待卡拿开读卡器，用来避免同一次贴卡被当成多次刷卡。
   *
   * @param maxMs 最长等待时间（毫秒）。
   * @return 在时限内确认卡已拿开返回 true，否则返回 false。
   */
  bool waitForRemoval(uint32_t maxMs);

 private:
  /** 发送一条 APDU 并取回响应，成功指通信成功，状态字由调用方检查。 */
  bool exchange(const uint8_t* cmd, size_t cmdLen, uint8_t* resp, uint8_t* respLen);

  /** 在已激活的卡上读出卡号。 */
  bool readCardNumber(uint8_t out[kCardNumberBytes]);

  /** 重新初始化 I2C 和芯片，用于总线被拉死之后的恢复。 */
  bool recover();

  uint32_t pollCount_ = 0;
  uint8_t recoverAttempts_ = 0;
};