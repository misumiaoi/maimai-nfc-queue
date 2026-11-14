// 在电脑上测试不依赖硬件的逻辑：卡号解析、队列状态机和名字绑定。
// 运行：test/run.sh

#include <stdio.h>
#include <string.h>

#include "CardId.h"
#include "NameRegistry.h"
#include "QueueManager.h"
#include "Registrar.h"
#include "RowLabel.h"
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

static bool containsBytes(const uint8_t* hay, size_t hayLen, const uint8_t* needle,
                           size_t needleLen) {
  if (needleLen == 0 || hayLen < needleLen) return false;
  for (size_t i = 0; i + needleLen <= hayLen; ++i) {
    if (memcmp(hay + i, needle, needleLen) == 0) return true;
  }
  return false;
}

static void expectName(const char* in, const char* want) {
  char out[kMaxNameLen + 1];
  CHECK(normalizeName(in, out, sizeof(out)));
  CHECK(strcmp(out, want) == 0);
  CHECK(normalizeName(out, out, sizeof(out)));
  CHECK(strcmp(out, want) == 0);
}

static void testNormalizeName() {
  expectName("Alice", "Alice");
  expectName("  Alice  ", "Alice");
  expectName("Aoi  Chan", "Aoi Chan");
  expectName("Aoi - Chan", "Aoi - Chan");
  expectName("Bob-1", "Bob-1");
  expectName("Abcdefghijkl", "Abcdefghijkl");
  expectName("A", "A");
  expectName("12", "12");

  char out[kMaxNameLen + 1];
  CHECK(!normalizeName("", out, sizeof(out)));
  CHECK(!normalizeName("   ", out, sizeof(out)));
  CHECK(!normalizeName("你好", out, sizeof(out)));
  CHECK(!normalizeName("Alice!", out, sizeof(out)));
  CHECK(!normalizeName("Abcdefghijklm", out, sizeof(out)));
  CHECK(!normalizeName("-Aoi", out, sizeof(out)));
  CHECK(!normalizeName("Aoi-", out, sizeof(out)));
  CHECK(!normalizeName("Aoi-", out, sizeof(out)));
  CHECK(!normalizeName(nullptr, out, sizeof(out)));
  CHECK(!normalizeName("A", out, 4));
}

static void testNameLine() {
  LineCommand cmd = parseNameLine("NAME Alice");
  CHECK(cmd.kind == LineKind::Bind && strcmp(cmd.name, "Alice") == 0);
  cmd = parseNameLine("name   Aoi  Chan");
  CHECK(cmd.kind == LineKind::Bind && strcmp(cmd.name, "Aoi Chan") == 0);
  cmd = parseNameLine("CANCEL");
  CHECK(cmd.kind == LineKind::Cancel);
  cmd = parseNameLine("  cancel  ");
  CHECK(cmd.kind == LineKind::Cancel);
  cmd = parseNameLine("");
  CHECK(cmd.kind == LineKind::Empty);
  cmd = parseNameLine("   ");
  CHECK(cmd.kind == LineKind::Empty);
  cmd = parseNameLine("NAME");
  CHECK(cmd.kind == LineKind::Invalid);
  cmd = parseNameLine("NAME !!!");
  CHECK(cmd.kind == LineKind::Invalid);
  cmd = parseNameLine("HELLO");
  CHECK(cmd.kind == LineKind::Invalid);
  cmd = parseNameLine(nullptr);
  CHECK(cmd.kind == LineKind::Invalid);
}

static void testNameRegistry() {
  NameRegistry reg;
  CardId a = cardWithTail(0x11, 0x11);
  CardId b = cardWithTail(0x22, 0x22);
  const uint8_t raw[kCardNumberBytes] = {0x03, 0x10, 0x51, 0x70, 0x01, 0x01, 0x70, 0x70, 0x11, 0x11};

  CHECK(reg.bind(a.hash, "  Alice  "));
  CHECK(strcmp(reg.find(a.hash), "Alice") == 0);
  CHECK(reg.find(b.hash) == nullptr);
  CHECK(reg.bind(a.hash, "Aoi"));
  CHECK(reg.size() == 1);
  CHECK(strcmp(reg.find(a.hash), "Aoi") == 0);
  CHECK(!reg.bind(b.hash, "你好"));
  CHECK(reg.size() == 1);

  uint8_t buf[NameRegistry::kMaxSerializedBytes];
  size_t n = reg.serialize(buf, sizeof(buf));
  CHECK(n == 3 + (8 + kMaxNameLen));
  CHECK(reg.serialize(buf, 2) == 0);
  CHECK(!containsBytes(buf, n, raw, sizeof(raw)));
  CHECK(buf[0] == 'N' && buf[1] == '1');

  NameRegistry loaded;
  loaded.bind(b.hash, "Keep");
  CHECK(loaded.deserialize(buf, n));
  CHECK(loaded.size() == 1);
  CHECK(strcmp(loaded.find(a.hash), "Aoi") == 0);
  CHECK(loaded.find(b.hash) == nullptr);

  CHECK(!loaded.deserialize(buf, n - 1));
  CHECK(loaded.size() == 1);
  buf[0] = 'Q';
  CHECK(!loaded.deserialize(buf, n));
  CHECK(strcmp(loaded.find(a.hash), "Aoi") == 0);

  NameRegistry full;
  char name[kMaxNameLen + 1];
  for (size_t i = 0; i < kMaxNames; ++i) {
    snprintf(name, sizeof(name), "P%u", static_cast<unsigned>(i));
    CHECK(full.bind(i + 1, name));
  }
  CHECK(!full.bind(kMaxNames + 1, "Extra"));
  CHECK(full.bind(1, "Updated"));
  CHECK(strcmp(full.find(1), "Updated") == 0);
  CHECK(full.size() == kMaxNames);
}

static void testBindWindow() {
  NameRegistry names;
  Registrar reg(60000);
  CardId a = cardWithTail(0x11, 0x11);
  CardId b = cardWithTail(0x22, 0x22);

  CHECK(reg.consume(a.hash, names, 0) == Registrar::BindTap::None);
  CHECK(!reg.arm("!!!", 0));
  CHECK(!reg.armed());

  CHECK(reg.arm("  Alice ", 1000));
  CHECK(reg.armed());
  CHECK(strcmp(reg.pending(), "Alice") == 0);
  CHECK(reg.consume(a.hash, names, 61000) == Registrar::BindTap::Bound);
  CHECK(!reg.armed());
  CHECK(strcmp(names.find(a.hash), "Alice") == 0);
  CHECK(names.find(b.hash) == nullptr);

  CHECK(reg.arm("Bob", 0));
  reg.cancel();
  CHECK(reg.consume(a.hash, names, 10) == Registrar::BindTap::None);
  CHECK(strcmp(names.find(a.hash), "Alice") == 0);

  CHECK(reg.arm("Cara", 100));
  CHECK(!reg.expire(60100));
  CHECK(reg.expire(60101));
  CHECK(!reg.armed());
  CHECK(reg.consume(b.hash, names, 70000) == Registrar::BindTap::None);

  CHECK(reg.arm("Dora", 0xFFFFFFF0u));
  CHECK(reg.consume(b.hash, names, 0x00000020u) == Registrar::BindTap::Bound);
  CHECK(strcmp(names.find(b.hash), "Dora") == 0);

  CHECK(reg.arm("Late", 0));
  CHECK(reg.consume(a.hash, names, 60001) == Registrar::BindTap::Expired);
  CHECK(!reg.armed());
  CHECK(strcmp(names.find(a.hash), "Alice") == 0);

  CHECK(reg.arm("Alice", 0));
  CHECK(reg.arm("Bob", 50000));
  CHECK(reg.consume(a.hash, names, 60000) == Registrar::BindTap::Bound);
  CHECK(strcmp(names.find(a.hash), "Bob") == 0);
}

static void testBindThenQueue() {
  QueueManager q(5000);
  NameRegistry names;
  Registrar reg(60000);
  CardId a = cardWithTail(0x11, 0x11);
  CardId b = cardWithTail(0x22, 0x22);
  CardId c = cardWithTail(0x33, 0x33);

  CHECK(reg.arm("Alice", 0));
  CHECK(reg.consume(a.hash, names, 1000) == Registrar::BindTap::Bound);
  CHECK(q.size() == 0);

  TapResult r = q.handleTap(a, 2000);
  CHECK(r.outcome == TapOutcome::Joined && r.position == 1);
  char label[kRowLabelCap];
  formatRowLabel(q, names, 0, label);
  CHECK(strcmp(label, "Alice") == 0);

  r = q.handleTap(a, 3000);
  CHECK(r.outcome == TapOutcome::ConfirmLeave && r.position == 1);
  r = q.handleTap(a, 4000);
  CHECK(r.outcome == TapOutcome::Left && r.position == 1);
  CHECK(q.size() == 0);
  CHECK(strcmp(names.find(a.hash), "Alice") == 0);

  names.bind(a.hash, "Aoi");
  names.bind(b.hash, "Aoi");
  q.handleTap(a, 0);
  q.handleTap(b, 0);
  q.handleTap(c, 0);
  formatRowLabel(q, names, 0, label);
  CHECK(strcmp(label, "Aoi 1111") == 0);
  formatRowLabel(q, names, 1, label);
  CHECK(strcmp(label, "Aoi 2222") == 0);
  formatRowLabel(q, names, 2, label);
  char tail[kTailDigits + 1];
  q.label(2, tail);
  CHECK(strcmp(label, tail) == 0);

  NameRegistry cases;
  cases.bind(a.hash, "Aoi");
  cases.bind(b.hash, "aoi");
  formatRowLabel(q, cases, 0, label);
  CHECK(strcmp(label, "Aoi") == 0);
  formatRowLabel(q, cases, 1, label);
  CHECK(strcmp(label, "aoi") == 0);

  uint8_t stored[QueueManager::kMaxSerializedBytes];
  size_t n = q.serialize(stored, sizeof(stored));
  CHECK(n == 3 + 3 * (8 + kTailDigits));
  const uint8_t raw[kCardNumberBytes] = {0x03, 0x10, 0x51, 0x70, 0x01, 0x01, 0x70, 0x70, 0x11, 0x11};
  CHECK(!containsBytes(stored, n, raw, sizeof(raw)));
  CHECK(containsBytes(stored, n, reinterpret_cast<const uint8_t*>("701111"), 6));
}

static void testRegistryFullTap() {
  NameRegistry names;
  Registrar reg(60000);
  char name[kMaxNameLen + 1];
  for (size_t i = 0; i < kMaxNames; ++i) {
    snprintf(name, sizeof(name), "N%u", static_cast<unsigned>(i));
    CHECK(names.bind(1000 + i, name));
  }
  CHECK(reg.arm("Extra", 0));
  CHECK(reg.consume(7, names, 10) == Registrar::BindTap::Full);
  CHECK(!reg.armed());
  CHECK(names.find(7) == nullptr);
  CHECK(names.size() == kMaxNames);
}

int main() {
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
  testNormalizeName();
  testNameLine();
  testNameRegistry();
  testBindWindow();
  testBindThenQueue();
  testRegistryFullTap();

  if (g_failed == 0) {
    printf("all tests passed\n");
    return 0;
  }
  printf("%d check(s) failed\n", g_failed);
  return 1;
}
