#pragma once

#include "NameRegistry.h"
#include "QueueManager.h"

/** formatRowLabel 写入的缓冲区大小，含结尾的空字符。 */
constexpr size_t kRowLabelCap = kMaxNameLen + 1 + kTailDigits + 1;

/**
 * 生成队列第 i 行要画到墨水屏上的文字。
 *
 * 绑过名字就显示名字。同一屏上有两个相同名字时，后面加上原来的末几位以便区分。
 * 没绑过名字的卡仍用卡号末几位（重号时加长到最多 6 位）。
 *
 * @param out 至少 kRowLabelCap 字节。
 */
void formatRowLabel(const QueueManager& queue, const NameRegistry& names, size_t i, char* out);
