// 在电脑上测试不依赖硬件的逻辑：卡号解析和队列状态机。
// 运行：test/run.sh

#include <stdio.h>
#include <string.h>

#include "CardId.h"
#include "CardPresence.h"
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

static void testContinuousPresence() {
  CardPresence presence(3);
  QueueManager q(5000);
  const CardId a = cardWithTail(0x11, 0x11);
  for (uint32_t now = 0; now <= 60000; now += 100) {
    if (presence.observe(true)) q.handleTap(a, now);
  }
  CHECK(q.size() == 1 && !q.hasPending());
  // 短暂漏读不能重新触发，连续三次无卡才算移开。
  presence.observe(false);
  presence.observe(false);
  CHECK(!presence.observe(true));
  for (int i = 0; i < 3; ++i) presence.observe(false);
  CHECK(!presence.waitingForRemoval());
  CHECK(presence.observe(true));
  CHECK(q.handleTap(a, 61000).outcome == TapOutcome::ConfirmLeave);
  for (int i = 0; i < 100; ++i) CHECK(!presence.observe(true));
  CHECK(q.size() == 1);
}

static void testPausedConfirmation() {
  QueueManager q(5000);
  const CardId a = cardWithTail(0x11, 0x11);
  q.handleTap(a, 0);
  q.handleTap(a, 100);
  q.pausePending();
  CHECK(!q.expirePending(60000));  // 慢刷新及一直贴卡不消耗窗口。
  q.resumePending(60000);
  q.resumePending(64000);  // 重复通知不能延长窗口。
  CHECK(!q.expirePending(65000));
  CHECK(q.expirePending(65001));

  q.handleTap(a, 70000);
  q.pausePending();
  q.resumePending(0xFFFFFF00u);
  CHECK(q.handleTap(a, 0x00000100u).outcome == TapOutcome::Left);

  q.handleTap(a, 0);
  q.handleTap(a, 1);
  q.pausePending();
  CHECK(q.handleTap(a, 2).outcome == TapOutcome::ConfirmLeave);
}

int main() {
  testContinuousPresence();
  testPausedConfirmation();
  testParse();
  testCardId();
  testJoinAndLeave();
  testConfirmTimeout();
  testOtherCardClearsPending();
  testTimeWrap();
  testFull();
  testLabelCollision();
  testRemoveHead();
  testSerialize();

  if (g_failed == 0) {
    printf("all tests passed\n");
    return 0;
  }
  printf("%d check(s) failed\n", g_failed);
  return 1;
}
