#pragma once
#include <stdint.h>

constexpr int LOW = 0;
constexpr int HIGH = 1;
constexpr int INPUT_PULLUP = 2;
inline uint32_t fakeNow = 0;
inline int fakeButton = HIGH;
inline uint32_t millis() { return fakeNow; }
inline void delay(uint32_t ms) { fakeNow += ms; }
inline int digitalRead(uint8_t) { return fakeButton; }
inline void pinMode(uint8_t, int) {}
inline void setCpuFrequencyMhz(uint32_t) {}
struct FakeSerial { void begin(int) {} };
inline FakeSerial Serial;
