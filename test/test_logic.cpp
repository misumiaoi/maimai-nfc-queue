// 在电脑上测试不依赖硬件的逻辑：卡号解析和队列状态机。
// 运行：test/run.sh

#include <stdio.h>
#include <string.h>

#include "CardId.h"
#include "QueueManager.h"
#include "TUnion.h"
#include "TapGate.h"
#include "config.h"

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
  CHECK(strcmp(id.tail, "123456") == 0);

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

  // 散列算法固定下来：改了它，升级固件后闪存里旧队列的卡都会认不出来。
  const uint8_t m[kCardNumberBytes] = {0x03, 0x10, 0x51, 0x70, 0x01, 0x01, 0x70, 0x12, 0x34, 0x56};
  CHECK(makeCardId(m, 0x1122334455667788ULL).hash == 0x978693c8f57d31abULL);
}

static void testUidCardId() {
  const uint8_t uid7[] = {0x04, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
  CardId a = makeUidCardId(uid7, sizeof(uid7), 1);
  CHECK(strcmp(a.tail, "D4E5F6") == 0);
  CHECK(makeUidCardId(uid7, sizeof(uid7), 1).hash == a.hash);
  CHECK(makeUidCardId(uid7, sizeof(uid7), 2).hash != a.hash);

  // 长度不同的 UID 即使前缀相同也是不同的卡。
  CHECK(makeUidCardId(uid7, 4, 1).hash != a.hash);

  // 短 UID 的显示末尾前面补 0。
  const uint8_t uid2[] = {0xAB, 0xCD};
  CHECK(strcmp(makeUidCardId(uid2, 2, 1).tail, "00ABCD") == 0);

  // 10 字节的 UID 和 10 字节的卡号字节完全相同，散列也不能相同。
  const uint8_t same[kCardNumberBytes] = {0x03, 0x10, 0x51, 0x70, 0x01, 0x01, 0x70, 0x12, 0x34, 0x56};
  CHECK(makeUidCardId(same, sizeof(same), 1).hash != makeCardId(same, 1).hash);
}

static void testFixedUid() {
  const uint8_t random4[] = {0x08, 0x12, 0x34, 0x56};
  const uint8_t fixed4[] = {0x5A, 0x12, 0x34, 0x56};
  const uint8_t seven[] = {0x08, 1, 2, 3, 4, 5, 6};
  const uint8_t ten[] = {0x08, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  CHECK(!isFixedUid(random4, 4));
  CHECK(isFixedUid(fixed4, 4));
  CHECK(isFixedUid(seven, 7));
  CHECK(isFixedUid(ten, 10));
  CHECK(!isFixedUid(fixed4, 0));
  CHECK(!isFixedUid(fixed4, 3));
}

static void testGateHoldsWhileCardStays() {
  TapGate g(3, 1000);
  CHECK(g.accepts(1, 0));
  g.onHandled(1);
  CHECK(g.waitingForRemoval());
  CHECK(!g.accepts(2, 0));

  // 卡一直贴着，多久都不放行。
  for (uint32_t t = 0; t < 60000; t += 100) {
    CHECK(!g.observe(true, t));
  }
  CHECK(g.waitingForRemoval());

  // 断续读不到不够 3 次，不算拿开。
  CHECK(!g.observe(false, 60000));
  CHECK(!g.observe(false, 60100));
  CHECK(!g.observe(true, 60200));
  CHECK(!g.observe(false, 60300));
  CHECK(!g.observe(false, 60400));
  CHECK(g.observe(false, 60500));
  CHECK(!g.waitingForRemoval());
}

static void testGateCooldown() {
  TapGate g(1, 1000);
  g.onHandled(7);
  CHECK(g.observe(false, 5000));

  // 同一张卡在冷却时间内被忽略，别的卡不受影响。
  CHECK(!g.accepts(7, 5500));
  CHECK(g.accepts(8, 5500));

  // 被忽略的那次拿开后，冷却时间仍从第一次拿开算。
  g.onIgnored();
  CHECK(g.observe(false, 5800));
  CHECK(!g.accepts(7, 5900));
  CHECK(g.accepts(7, 6000));
}

static void testGateUnsupportedCard() {
  TapGate g(2, 1000);
  g.onIgnored();
  CHECK(g.waitingForRemoval());
  CHECK(!g.observe(false, 0));
  CHECK(g.observe(false, 100));
  CHECK(g.accepts(1, 100));
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

static void testRestartPendingTimer() {
  QueueManager q(5000);
  CardId a = cardWithTail(0x11, 0x11);
  q.handleTap(a, 0);
  q.handleTap(a, 1000);
  // 屏幕刷新用了 4 秒，从刷新完开始计时。
  q.restartPendingTimer(5000);
  CHECK(!q.expirePending(9000));
  TapResult r = q.handleTap(a, 9500);
  CHECK(r.outcome == TapOutcome::Left);

  // 没有待确认的卡时什么都不做。
  q.restartPendingTimer(20000);
  CHECK(!q.hasPending());
}

static void testOtherCardClearsPending() {
  QueueManager q(5000);
  CardId a = cardWithTail(0x11, 0x11);
  CardId b = cardWithTail(0x22, 0x22);
  CardId c = cardWithTail(0x33, 0x33);
  q.handleTap(a, 0);
  q.handleTap(b, 0);

  q.handleTap(a, 100);
  CHECK(q.hasPending());
  q.handleTap(c, 200);
  CHECK(!q.hasPending());
  TapResult r = q.handleTap(a, 300);
  CHECK(r.outcome == TapOutcome::ConfirmLeave);
  CHECK(q.size() == 3);
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

static void testClear() {
  QueueManager q(5000);
  CHECK(!q.clear());
  CardId a = cardWithTail(0x11, 0x11);
  q.handleTap(a, 0);
  q.handleTap(cardWithTail(0x22, 0x22), 0);
  q.handleTap(a, 100);
  CHECK(q.hasPending());
  CHECK(q.clear());
  CHECK(q.size() == 0 && !q.hasPending());
  TapResult r = q.handleTap(a, 200);
  CHECK(r.outcome == TapOutcome::Joined && r.position == 1);
}

static void testBeepCounts() {
  CHECK(config::kBeepsTap == 1);
  CHECK(config::kBeepsStep == 1);
  CHECK(config::kBeepsDelete == 2);
  CHECK(config::kBeepsError == 3);
  CHECK(config::kBeepOnMs > 0);
  CHECK(config::kBeepGapMs > 0);
}

static void testSerialize() {
  QueueManager q(5000);
  CardId a = cardWithTail(0x11, 0x11);
  CardId b = cardWithTail(0x22, 0x22);
  q.handleTap(a, 0);
  q.handleTap(b, 0);

  uint8_t buf[QueueManager::kMaxSerializedBytes];
  size_t n = q.serialize(buf, sizeof(buf));
  CHECK(n > 0);
  CHECK(q.serialize(buf, 2) == 0);

  QueueManager r(5000);
  CHECK(r.deserialize(buf, n));
  CHECK(r.size() == 2);
  CHECK(r.at(0).hash == a.hash && r.at(1).hash == b.hash);
  CHECK(strcmp(r.at(1).tail, b.tail) == 0);

  QueueManager bad(5000);
  bad.handleTap(a, 0);
  CHECK(!bad.deserialize(buf, n - 1));
  buf[0] = 'X';
  CHECK(!bad.deserialize(buf, n));
  CHECK(bad.size() == 1);
}

int main() {
  testParse();
  testCardId();
  testUidCardId();
  testFixedUid();
  testGateHoldsWhileCardStays();
  testGateCooldown();
  testGateUnsupportedCard();
  testJoinAndLeave();
  testConfirmTimeout();
  testRestartPendingTimer();
  testOtherCardClearsPending();
  testTimeWrap();
  testFull();
  testLabelCollision();
  testRemoveHead();
  testClear();
  testBeepCounts();
  testSerialize();

  if (g_failed == 0) {
    printf("all tests passed\n");
    return 0;
  }
  printf("%d check(s) failed\n", g_failed);
  return 1;
}
