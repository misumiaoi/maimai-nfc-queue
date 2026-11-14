#pragma once

#include <stddef.h>
#include <stdint.h>

#include "CardId.h"

/**
 * 交通联合卡（T-Union，JT/T 978）的只读协议封装。
 *
 * 只做三件事：给出要发给卡的 SELECT 命令、判断响应是否成功、从响应里取出卡号。
 * 不读余额、不读交易记录、不写卡。本文件不依赖 Arduino，可以在电脑上直接测试。
 */
namespace tunion {

/** SELECT 支付环境 PPSE（2PAY.SYS.DDF01）的 APDU。 */
extern const uint8_t kSelectPpse[];
extern const size_t kSelectPpseLen;

/** SELECT 电子钱包应用 A000000632010105 的 APDU，响应的 FCI 里带卡号。 */
extern const uint8_t kSelectWalletAid[];
extern const size_t kSelectWalletAidLen;

/**
 * 判断 APDU 响应的状态字是否为 90 00（成功）。
 *
 * @param resp 卡返回的完整响应（数据 + SW1 SW2）。
 * @param len  响应长度。
 */
bool isSuccess(const uint8_t* resp, size_t len);

/**
 * 从 SELECT 电子钱包应用的响应里取出 10 字节卡号。
 *
 * 响应是 BER-TLV：6F(FCI) -> A5 -> 9F0C(发卡方自定义数据，30 字节)。
 * 卡号是 9F0C 值里偏移 10 开始的 10 字节 BCD。
 *
 * @param resp 完整响应，末尾含 SW1 SW2。
 * @param len  响应长度。
 * @param out  输出：10 字节卡号。
 * @return 响应合法且卡号有效返回 true；找不到 9F0C、长度不够、状态字不是 9000、
 *         或卡号全 00 / 全 FF 时返回 false。
 */
bool parseCardNumber(const uint8_t* resp, size_t len, uint8_t out[kCardNumberBytes]);

}  // namespace tunion
