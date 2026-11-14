// 在电脑上测试不依赖硬件的逻辑：卡号解析、队列状态机、序列化格式。
// 运行：host_test/run.sh

#include <stdio.h>
#include <string.h>

#include "CardId.h"
#include "QueueManager.h"
#include "TUnion.h"

static int g_failed = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
      ++g_failed;                                                      \
    }                                                                  \
  } while (0)

static CardId cardWithTail(uint8_t b8, uint8_t b9, uint8_t b7 = 0x70) {
  uint8_t n[kCardNumberBytes] = {0x03, 0x10, 0x51, 0x70, 0x01, 0x01, 0x70, b7, b8, b9};
  return makeCardId(n, 0x1122334455667788ULL);
}

// 取自公开资料里一次 SELECT 钱包应用的响应，卡号末 6 位替换为 123456。
static const uint8_t kSampleResp[] = {
    0x6F, 0x31, 0x84, 0x08, 0xA0, 0x00, 0x00, 0x06, 0x32, 0x01, 0x01, 0x05, 0xA5, 0x25, 0x9F,
    0x08, 0x01, 0x02, 0x9F, 0x0C, 0x1E, 0x01, 0x01, 0x10, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x02,
    0x01, 0x03, 0x10, 0x51, 0x70, 0x01, 0x01, 0x70, 0x12, 0x34, 0x56, 0x20, 0x19, 0x04, 0x15,
    0x20, 0x40, 0x12, 0x31, 0x00, 0x00, 0x90, 0x00};

static void testParse() {
  uint8_t num[kCardNumberBytes];
  CHECK(tunion::parseCardNumber(kSampleResp, sizeof(kSampleResp), num));
  const uint8_t want[kCardNumberBytes] = {0x03, 0x10, 0x51, 0x70, 0x01, 0x01, 0x70, 0x12, 0x34, 0x56};
  CHECK(memcmp(num, want, sizeof(want)) == 0);

  CardId id = makeCardId(num, 1);
  CHECK(strlen(id.tail) == kTailDigits);
  CHECK(strcmp(id.tail + kTailDigits - 6, "123456") == 0);

  uint8_t bad[sizeof(kSampleResp)];
  memcpy(bad, kSampleResp, sizeof(bad));
  bad[sizeof(bad) - 2] = 0x6A;
  bad[sizeof(bad) - 1] = 0x82;
  CHECK(!tunion::parseCardNumber(bad, sizeof(bad), num));

  CHECK(!tunion::parseCardNumber(kSampleResp, 10, num));
  CHECK(!tunion::parseCardNumber(kSampleResp, 0, num));

  uint8_t zero[sizeof(kSampleResp)];
  memcpy(zero, kSampleResp, sizeof(zero));
  memset(zero + 31, 0x00, kCardNumberBytes);
  CHECK(!tunion::parseCardNumber(zero, sizeof(zero), num));

  for (size_t cut = 0; cut < sizeof(kSampleResp); ++cut) {
    tunion::parseCardNumber(kSampleResp, cut, num);
  }
}

static void testCardId() {
  CardId a = cardWithTail(0x12, 0x34);
  CardId b = cardWithTail(0x12, 0x35);
  CHECK(a.hash != b.hash);

  uint8_t n[kCardNumberBytes] = {0x03, 0x10, 0x51, 0x70, 0x01, 0x01, 0x70, 0x70, 0x12, 0x34};
  CHECK(makeCardId(n, 1).hash != makeCardId(n, 2).hash);
  CHECK(makeCardId(n, 1).hash == makeCardId(n, 1).hash);
}

static void testJoinAndLeave() {
  QueueManager q(5000);
  CardId a = cardWithTail(0x11, 0x11);
  CardId b = cardWithTail(0x22, 0x22);

  TapResult r = q.handleTap(a, 1000);
  CHECK(r.outcome == TapOutcome::Joined && r.position == 1);
  r = q.handleTap(b, 2000);
  CHECK(r.outcome == TapOutcome::Joined && r.position == 2);

  r = q.handleTap(a, 3000);
  CHECK(r.outcome == TapOutcome::ConfirmLeave && r.position == 1);
  CHECK(q.hasPending() && q.pendingIndex() == 0);
  CHECK(q.isPending(0) && !q.isPending(1));
  CHECK(q.size() == 2);

  r = q.handleTap(a, 4000);
  CHECK(r.outcome == TapOutcome::Left && r.position == 1);
  CHECK(q.size() == 1);
  CHECK(q.at(0).hash == b.hash);
  CHECK(!q.hasPending());
}

static void testConfirmTimeout() {
  QueueManager q(5000);
  CardId a = cardWithTail(0x11, 0x11);
  q.handleTap(a, 0);

  q.handleTap(a, 1000);
  CHECK(!q.expirePending(5000));
  CHECK(q.expirePending(6001));
  CHECK(!q.hasPending());

  TapResult r = q.handleTap(a, 7000);
  CHECK(r.outcome == TapOutcome::ConfirmLeave);
  r = q.handleTap(a, 13000);
  CHECK(r.outcome == TapOutcome::ConfirmLeave);
  CHECK(q.size() == 1);
}

// 别人刷卡不该把你这张的待确认清掉（旧版是全局单槽，忙的时候会失效）。
static void testPendingIsPerCard() {
  QueueManager q(5000);
  CardId a = cardWithTail(0x11, 0x11);
  CardId b = cardWithTail(0x22, 0x22);
  CardId c = cardWithTail(0x33, 0x33);
  q.handleTap(a, 0);
  q.handleTap(b, 0);

  q.handleTap(a, 100);
  CHECK(q.isPending(0));
  q.handleTap(c, 200);
  CHECK(q.isPending(0));
  TapResult r = q.handleTap(a, 300);
  CHECK(r.outcome == TapOutcome::Left);
  CHECK(q.size() == 2);

  // 两张卡各自待确认，互不干扰。
  q.handleTap(b, 400);
  q.handleTap(c, 500);
  CHECK(q.isPending(0) && q.isPending(1));
  CHECK(q.pendingIndex() == 0);
}

static void testTimeWrap() {
  QueueManager q(5000);
  CardId a = cardWithTail(0x11, 0x11);
  q.handleTap(a, 0xFFFFFF00u);
  q.handleTap(a, 0xFFFFFF10u);
  TapResult r = q.handleTap(a, 0x00000100u);
  CHECK(r.outcome == TapOutcome::Left);
}

static void testFull() {
  QueueManager q(5000);
  for (size_t i = 0; i < kMaxQueue; ++i) {
    TapResult r = q.handleTap(cardWithTail(static_cast<uint8_t>(i), 0x01), 0);
    CHECK(r.outcome == TapOutcome::Joined);
  }
  TapResult r = q.handleTap(cardWithTail(0xEE, 0xEE), 0);
  CHECK(r.outcome == TapOutcome::QueueFull && r.position == 0);
  CHECK(q.size() == kMaxQueue);
}

static void testLabelCollision() {
  QueueManager q(5000);
  q.handleTap(cardWithTail(0x12, 0x34, 0x01), 0);
  q.handleTap(cardWithTail(0x12, 0x34, 0x02), 0);
  q.handleTap(cardWithTail(0x99, 0x99, 0x03), 0);

  char l[kTailDigits + 1];
  CHECK(q.labelLength(2) == 4);
  q.label(2, l);
  CHECK(strcmp(l, "9999") == 0);

  CHECK(q.labelLength(0) == 5);
  q.label(0, l);
  CHECK(strcmp(l, "11234") == 0);
  q.label(1, l);
  CHECK(strcmp(l, "21234") == 0);
}

// 末尾 6 位全同、只有再往前两位不同的两张卡：
// 旧版显示成一样的号，现在能把第 7、8 位拉出来区分。
static void testLabelDeepCollision() {
  QueueManager q(5000);
  uint8_t a[kCardNumberBytes] = {0x03, 0x10, 0x51, 0x70, 0x01, 0x01, 0x01, 0x12, 0x34, 0x56};
  uint8_t b[kCardNumberBytes] = {0x03, 0x10, 0x51, 0x70, 0x01, 0x01, 0x02, 0x12, 0x34, 0x56};
  q.handleTap(makeCardId(a, 7), 0);
  q.handleTap(makeCardId(b, 7), 0);

  CHECK(q.size() == 2);
  char la[kTailDigits + 1];
  char lb[kTailDigits + 1];
  q.label(0, la);
  q.label(1, lb);
  CHECK(strcmp(la, lb) != 0);
}

static void testRemoveHead() {
  QueueManager q(5000);
  CHECK(!q.removeHead());
  CardId a = cardWithTail(0x11, 0x11);
  CardId b = cardWithTail(0x22, 0x22);
  q.handleTap(a, 0);
  q.handleTap(b, 0);
  CHECK(q.removeHead());
  CHECK(q.size() == 1 && q.at(0).hash == b.hash);
}

// 队伍中间的人走了也要能删掉，否则会变成僵尸占位。
static void testRemoveMiddle() {
  QueueManager q(5000);
  CardId a = cardWithTail(0x11, 0x11);
  CardId b = cardWithTail(0x22, 0x22);
  CardId c = cardWithTail(0x33, 0x33);
  q.handleTap(a, 0);
  q.handleTap(b, 0);
  q.handleTap(c, 0);

  CHECK(!q.removeAt(3));
  CHECK(q.removeAt(1));
  CHECK(q.size() == 2);
  CHECK(q.at(0).hash == a.hash && q.at(1).hash == c.hash);
}

static void testPlaySlots() {
  QueueManager q(5000, 2);  // 两台机，四个上机位
  for (int i = 0; i < 6; ++i) {
    q.handleTap(cardWithTail(static_cast<uint8_t>(i), 0x01), 1000);
  }
  CHECK(q.size() == 6);
  for (size_t i = 0; i < 4; ++i) CHECK(q.isPlaying(i));
  for (size_t i = 4; i < 6; ++i) CHECK(!q.isPlaying(i));

  CHECK(q.playingForMs(0, 1000 + 90 * 1000) == 90 * 1000);
  CHECK(q.playingForMs(5, 1000 + 90 * 1000) == 0);

  q.removeHead();
  q.refreshPlaySlots(2000);
  CHECK(q.size() == 5);
  CHECK(q.isPlaying(3));
  CHECK(!q.isPlaying(4));
}

static void testStaleExpiry() {
  QueueManager q(5000);
  CardId a = cardWithTail(0x11, 0x11);
  CardId b = cardWithTail(0x22, 0x22);
  q.handleTap(a, 1000);
  q.handleTap(b, 20000);

  CHECK(q.expireStale(3000, 0) == 0);         // 不启用
  CHECK(q.expireStale(25000, 60000) == 0);    // 还没到时间
  CHECK(q.expireStale(70000, 60000) == 1);    // a 超时，b 才 50 秒
  CHECK(q.size() == 1 && q.at(0).hash == b.hash);
}

// 从闪存恢复出来的条目没有入队时刻，要先 claim 一次，否则会被立刻判成残留。
static void testClaimStartupTimestamps() {
  QueueManager q(5000);
  CardId a = cardWithTail(0x11, 0x11);
  q.handleTap(a, 5000);

  uint8_t buf[QueueManager::kMaxSerializedBytes];
  const size_t n = q.serialize(buf, sizeof(buf));

  QueueManager r(5000);
  CHECK(r.deserialize(buf, n));
  CHECK(r.at(0).joinedMs == 0);
  r.claimStartupTimestamps(123);
  CHECK(r.at(0).joinedMs == 123);
  CHECK(r.expireStale(125, 60000) == 0);
}

static void testSerialize() {
  QueueManager q(5000);
  CardId a = cardWithTail(0x11, 0x11);
  CardId b = cardWithTail(0x22, 0x22);
  q.handleTap(a, 0);
  q.handleTap(b, 0);

  uint8_t buf[QueueManager::kMaxSerializedBytes];
  const size_t n = q.serialize(buf, sizeof(buf));
  CHECK(n > 0);
  CHECK(n == 3 + 2 * (8 + kTailDigits) + 2);
  CHECK(q.serialize(buf, 2) == 0);

  QueueManager r(5000);
  CHECK(r.deserialize(buf, n));
  CHECK(r.size() == 2);
  CHECK(r.at(0).hash == a.hash && r.at(1).hash == b.hash);
  CHECK(strcmp(r.at(1).tail, b.tail) == 0);

  // 长度不对、魔数不对、校验和被改坏，都要拒绝，并且不动原队列。
  QueueManager bad(5000);
  bad.handleTap(a, 0);
  CHECK(!bad.deserialize(buf, n - 1));
  CHECK(!bad.deserialize(nullptr, n));
  CHECK(bad.size() == 1);

  uint8_t broken[QueueManager::kMaxSerializedBytes];
  memcpy(broken, buf, n);
  broken[0] = 'X';
  CHECK(!bad.deserialize(broken, n));

  memcpy(broken, buf, n);
  broken[5] ^= 0xFF;
  CHECK(!bad.deserialize(broken, n));
  CHECK(bad.size() == 1);

  memcpy(broken, buf, n);
  broken[n - 1] ^= 0x01;
  CHECK(!bad.deserialize(broken, n));
}

// NVS 被写坏成重复条目时，旧版会照收；现在识别为损坏。
static void testSerializeRejectsDuplicates() {
  QueueManager q(5000);
  CardId a = cardWithTail(0x11, 0x11);
  q.handleTap(a, 0);

  uint8_t buf[QueueManager::kMaxSerializedBytes];
  size_t n = q.serialize(buf, sizeof(buf));
  const size_t entry = 8 + kTailDigits;

  // 拼一个含两条相同散列的 blob，并补上正确的校验和。
  memcpy(buf + 3 + entry, buf + 3, entry);
  buf[2] = 2;
  n = 3 + 2 * entry + 2;
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n - 2; ++i) {
    crc ^= static_cast<uint16_t>(buf[i]) << 8;
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  buf[n - 2] = static_cast<uint8_t>(crc & 0xFF);
  buf[n - 1] = static_cast<uint8_t>(crc >> 8);

  QueueManager r(5000);
  CHECK(!r.deserialize(buf, n));
  CHECK(r.size() == 0);
}

// 旧固件写下的 v1 blob（无校验、6 位数字）要能读出来，方便直接升级。
static void testLegacyFormat() {
  // 'Q' '1' 1 条，散列 8 字节，末尾 6 位数字 '1'..'6'（v1 存的是数字字符本身）。
  const uint8_t v1[] = {0x51, 0x31, 0x01, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                        0x77, 0x88, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36};
  QueueManager q(5000);
  CHECK(q.deserialize(v1, sizeof(v1)));
  CHECK(q.size() == 1);
  CHECK(strcmp(q.at(0).tail + kTailDigits - 6, "123456") == 0);

  uint8_t v2[QueueManager::kMaxSerializedBytes];
  const size_t n = q.serialize(v2, sizeof(v2));
  CHECK(v2[1] == '2');
  QueueManager again(5000);
  CHECK(again.deserialize(v2, n));
  CHECK(strcmp(again.at(0).tail, q.at(0).tail) == 0);
}

int main() {
  testParse();
  testCardId();
  testJoinAndLeave();
  testConfirmTimeout();
  testPendingIsPerCard();
  testTimeWrap();
  testFull();
  testLabelCollision();
  testLabelDeepCollision();
  testRemoveHead();
  testRemoveMiddle();
  testPlaySlots();
  testStaleExpiry();
  testClaimStartupTimestamps();
  testSerialize();
  testSerializeRejectsDuplicates();
  testLegacyFormat();

  if (g_failed == 0) {
    printf("all tests passed\n");
    return 0;
  }
  printf("%d check(s) failed\n", g_failed);
  return 1;
}