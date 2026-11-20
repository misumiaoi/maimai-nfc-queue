#pragma once

#include <stddef.h>
#include <stdint.h>

#include "CardId.h"

/** 一次轮询的状态。 */
enum class ReadStatus {
  NoCard,       ///< 读卡器范围内没有卡，或卡在读的过程中拿走了。
  Transit,      ///< 读到交通联合卡号，number 有效。
  FixedUid,     ///< 不是交通联合卡，但 UID 固定，可以用 uid 识别（例如 aime 卡）。
  Unsupported,  ///< 有卡，但既读不出卡号，UID 也是随机的（例如没设快捷交通卡的手机）。
};

/** CardReader::poll 的返回值。 */
struct CardReadResult {
  ReadStatus status;
  uint8_t number[kCardNumberBytes];  ///< status == Transit 时有效。
  uint8_t uid[kMaxUidBytes];         ///< status == FixedUid 时有效。
  uint8_t uidLen;
};

/**
 * PN532 读卡器封装（I2C）。
 *
 * 优先读交通联合卡的公开卡号，因为手机里的虚拟卡 UID 可能每次都变。卡号来自应用层的
 * SELECT 响应（见 TUnion.h），不读余额和交易记录，也不写卡。读不出卡号时，
 * 如果卡的 UID 是固定的，就退回用 UID 识别。
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
   * 读到卡后先尝试读交通联合卡号。卡对 APDU 完全没有应答时（Mifare Classic 这类
   * 不支持 ISO14443-4 的卡就是这样），重新寻卡再试一次，两次都没有应答才按 UID 处理。
   * 这样实体交通联合卡在读的过程中偶尔通信失败，不会被误认成另一张卡。
   */
  CardReadResult poll();

  /**
   * 检查一次读卡器范围内有没有卡，最多阻塞 config::kPollTimeoutMs。
   * 用来判断上一张卡有没有拿开。
   */
  bool isCardPresent();

 private:
  /** readCardNumber 的结果。 */
  enum class NumberRead {
    Ok,        ///< 读到卡号。
    Rejected,  ///< 卡有应答，但不是交通联合卡，或卡号无效。
    NoAnswer,  ///< 卡对 APDU 没有应答。
  };

  /** 发送一条 APDU 并取回响应，成功指通信成功，状态字由调用方检查。 */
  bool exchange(const uint8_t* cmd, size_t cmdLen, uint8_t* resp, uint8_t* respLen);

  /** 在已激活的卡上读出卡号。 */
  NumberRead readCardNumber(uint8_t out[kCardNumberBytes]);

  /** 寻卡一次，读到时填 uid 和 uidLen。 */
  bool detect(uint8_t uid[kMaxUidBytes], uint8_t* uidLen);
};
