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

/** 长按跳过按钮多久，才移除队首（毫秒）。 */
constexpr uint32_t kSkipHoldMs = 3000;

/**
 * 登记名字用的开放热点。手机连上后打开网页填写名字，再贴卡。
 * 热点上只有名字，卡号不经过 WiFi。
 */
constexpr char kWifiSsid[] = "maimai-queue";

/** 提交名字后，等待贴卡完成绑定的时间（毫秒）。超时则这次登记作废。 */
constexpr uint32_t kBindWindowMs = 60000;

/** 每局部刷新多少次后做一次整屏刷新，用来清除残影。 */
constexpr uint8_t kFullRefreshEvery = 20;

/** 屏幕上最多显示多少行队列。 */
constexpr uint8_t kVisibleRows = 7;

/** CPU 频率（MHz）。80 MHz 可以带登记用的热点，也比满频省电。 */
constexpr uint32_t kCpuMhz = 80;

}  // namespace config
