#pragma once

#include <stddef.h>
#include <stdint.h>

/** 显示名最长字符数。要能放进 4.2 寸屏的一行，并且现有字体只能画 ASCII。 */
constexpr size_t kMaxNameLen = 12;

/** 名字表最多保留多少张卡。只按散列索引，不随离队删除。 */
constexpr size_t kMaxNames = 64;

/**
 * 把用户输入收成可以上屏、可以保存的显示名。
 *
 * 去掉首尾空格，连续空格合成一个。只允许英文字母、数字、空格和连字符，
 * 长度 1 到 kMaxNameLen，不能以连字符开头或结尾。全角字符和标点会被拒绝，
 * 因为墨水屏字体画不出来。
 *
 * @param in     原始输入，可以长于 kMaxNameLen。
 * @param out    输出缓冲区。
 * @param outCap out 的容量，至少 kMaxNameLen + 1。
 * @return 输入能收成合法名字时返回 true，并写入以空字符结尾的结果。
 */
bool normalizeName(const char* in, char* out, size_t outCap);

/** 串口一行文本解析出的动作。 */
enum class LineKind {
  Empty,    ///< 空行，忽略。
  Cancel,   ///< 取消当前等待贴卡的登记。
  Bind,     ///< 名字有效，name 已规范化。
  Invalid,  ///< 认不出的命令，或名字不合法。
};

/** parseNameLine 的返回值。kind 不是 Bind 时 name 为空串。 */
struct LineCommand {
  LineKind kind;
  char name[kMaxNameLen + 1];
};

/**
 * 解析串口的一行。
 *
 * 接受 `NAME 名字`（命令不区分大小写）和 `CANCEL`。名字规则同 normalizeName。
 */
LineCommand parseNameLine(const char* line);

/**
 * 卡的显示名表。
 *
 * 每条记录是 salted hash 和显示名。不保存卡号，也不保存末几位；末几位仍只在队列项里。
 * 同一张卡再次登记会覆盖旧名字。表满时新卡失败，已有的卡仍可改名。
 */
class NameRegistry {
 public:
  NameRegistry();

  /**
   * 把名字绑到散列上。name 会先经过 normalizeName。
   *
   * @return 写入成功返回 true；名字不合法，或表已满且这是一张新卡时返回 false。
   */
  bool bind(uint64_t hash, const char* name);

  /**
   * 查找名字。
   *
   * @return 找到时指向内部缓冲区，没找到返回 nullptr。下次 bind 同一条记录后指针内容会变。
   */
  const char* find(uint64_t hash) const;

  /** 当前保存了多少个名字。 */
  size_t size() const { return count_; }

  /**
   * 序列化成字节串。格式：'N' '1'、数量、然后每条 8 字节小端散列和 kMaxNameLen 字节的名字。
   *
   * @return 写入的字节数；容量不足返回 0。
   */
  size_t serialize(uint8_t* buf, size_t cap) const;

  /**
   * 从字节串恢复。格式不合法时保持原表不变。
   *
   * @return 恢复成功返回 true。
   */
  bool deserialize(const uint8_t* buf, size_t len);

  /** 序列化后的最大字节数。 */
  static constexpr size_t kMaxSerializedBytes = 3 + kMaxNames * (8 + kMaxNameLen);

 private:
  int indexOf(uint64_t hash) const;

  uint64_t hashes_[kMaxNames];
  char names_[kMaxNames][kMaxNameLen + 1];
  size_t count_ = 0;
};
