#pragma once

#include <stdint.h>

/**
 * 全局配置：引脚、时间参数、显示参数。
 *
 * 接线（ESP32 DevKit）
 *   墨水屏 (SPI):  SCK=18  MOSI=23  CS=5  DC=17  RST=16  BUSY=4
 *   PN532 (I2C):   SDA=21  SCL=22   IRQ=25  RST=26   （模块上的拨码开关要拨到 I2C）
 *   管理按钮:       GPIO27 -> 按钮 -> GND（内部上拉）
 */
namespace config {

constexpr uint8_t kPinEpdSck = 18;
constexpr uint8_t kPinEpdMosi = 23;
constexpr uint8_t kPinEpdCs = 5;
constexpr uint8_t kPinEpdDc = 17;
constexpr uint8_t kPinEpdRst = 16;
constexpr uint8_t kPinEpdBusy = 4;

constexpr uint8_t kPinNfcSda = 21;
constexpr uint8_t kPinNfcScl = 22;
constexpr uint8_t kPinNfcIrq = 25;
constexpr uint8_t kPinNfcRst = 26;

constexpr uint8_t kPinSkipButton = 27;

/** 机厅的舞萌机台数。每台机两个人同时上机，所以屏幕上前 2 * 台数 个位置标 PLAY。 */
constexpr uint8_t kMachineCount = 1;

/**
 * 在队列里的卡第一次刷后，要在这个时间内再刷一次才算退出（毫秒）。
 * 从屏幕显示出“再刷一次退出”开始计时，刷新屏幕的时间不算在内。
 */
constexpr uint32_t kConfirmWindowMs = 5000;

/** 单次轮询卡片的超时（毫秒）。 */
constexpr uint16_t kPollTimeoutMs = 100;

/**
 * 连续多少次轮询不到卡，才认为卡已经拿开。
 * 处理完一次刷卡后会一直等到卡拿开，卡一直贴着不会被当成再刷一次。
 */
constexpr uint8_t kRemovalMisses = 3;

/** 同一张卡拿开后，在这个时间内又被读到就忽略，防止卡在读卡器边缘时时断时续（毫秒）。 */
constexpr uint32_t kSameCardCooldownMs = 1000;

/** 按住按钮这么久再松开，移除队首（毫秒）。 */
constexpr uint32_t kSkipHoldMs = 3000;

/** 按住按钮这么久再松开，清空整个队列（毫秒）。 */
constexpr uint32_t kClearHoldMs = 10000;

/** 每局部刷新多少次后做一次整屏刷新，用来清除残影。 */
constexpr uint8_t kFullRefreshEvery = 20;

/** 屏幕上最多显示多少行队列。 */
constexpr uint8_t kVisibleRows = 7;

/** CPU 频率（MHz）。不开无线，降频即可省电。 */
constexpr uint32_t kCpuMhz = 80;

}  // namespace config
