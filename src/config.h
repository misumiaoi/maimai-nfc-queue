#pragma once

#include <stdint.h>

/**
 * 全局配置：引脚、时间参数、显示参数。
 *
 * 接线（ESP32 DevKit）
 *   墨水屏 (SPI):  SCK=18  MOSI=23  CS=5  DC=17  RST=16  BUSY=4
 *   PN532 (I2C):   SDA=21  SCL=22   IRQ=25  RST=26   （模块上的拨码开关要拨到 I2C）
 *   跳过按钮:       GPIO27 -> 按钮 -> GND（内部上拉）
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

/** 在队列里的卡第一次刷后，要在这个时间内再刷一次才算退出（毫秒）。 */
constexpr uint32_t kConfirmWindowMs = 5000;

/** 处理完一次刷卡后，最长等待卡片离开读卡器的时间（毫秒）。 */
constexpr uint32_t kRemovalWaitMs = 3000;

/** 单次轮询卡片的超时（毫秒）。 */
constexpr uint16_t kPollTimeoutMs = 100;

/** 连续多少次轮询不到卡，才认为卡已经拿开。 */
constexpr uint8_t kRemovalMisses = 3;

/** 每隔多少次轮询做一次读卡器健康检查，检查失败会自动重连 PN532。 */
constexpr uint8_t kReaderHealthCheckEvery = 20;

/** 长按跳过按钮多久，进入移人模式（毫秒）。 */
constexpr uint32_t kSkipHoldMs = 3000;

/** 单次按住超过这个时长就不再累计，防止一直按着卡死流程（毫秒）。 */
constexpr uint32_t kMaxHoldMs = 6000;

/** 移人模式下多久没有操作就自动退出（毫秒）。 */
constexpr uint32_t kKickModeIdleMs = 8000;

/** 店员卡登记流程等卡的最长时间（毫秒）。 */
constexpr uint32_t kProvisionTimeoutMs = 15000;

/** 刷一次店员卡之后，按钮权限保留多久（毫秒）。 */
constexpr uint32_t kAdminUnlockMs = 60000;

/** 队列里超过这个时长的条目视为残留，自动移除（毫秒，0 表示不启用）。 */
constexpr uint32_t kStaleEntryMs = 6UL * 60UL * 60UL * 1000UL;

/** 一个上机位超过这个时长就在屏幕上标红提示，不自动踢人（毫秒）。 */
constexpr uint32_t kPlayLimitMs = 15UL * 60UL * 1000UL;

/** 上机时长显示的最短更新间隔（毫秒）。 */
constexpr uint32_t kClockRefreshMs = 60000;

/** 每局部刷新多少次后做一次整屏刷新，用来清除残影。 */
constexpr uint8_t kFullRefreshEvery = 20;

/** 屏幕上最多显示多少行队列。 */
constexpr uint8_t kVisibleRows = 7;

/** CPU 频率（MHz）。不开无线，降频即可省电。 */
constexpr uint32_t kCpuMhz = 80;

}  // namespace config