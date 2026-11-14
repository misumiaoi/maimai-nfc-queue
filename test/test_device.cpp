// 用模拟时钟、屏幕、读卡事件和 NVS 测试真实主循环与存储实现。
#include <assert.h>
#include <string>
#include <Preferences.h>

#include "../src/main.cpp"

static CardReadResult nextRead{};
static std::string lastMessage;
static int renders = 0;
static int polls = 0;
static bool readerOk = true;

bool CardReader::begin() { return readerOk; }
CardReadResult CardReader::poll() { ++polls; return nextRead; }
void QueueDisplay::begin() {}
void QueueDisplay::render(const QueueManager&, const char* message) {
  ++renders;
  lastMessage = message ? message : "";
  fakeNow += 10000;  // 故意让刷新比确认窗口更慢。
}
void QueueDisplay::showFatal(const char* message) { lastMessage = message; }

static void resetDevice() {
  fakeButton = HIGH;
  pollSkipButton();  // 清除按钮的静态状态。
  Preferences::reset();
  g_queue = QueueManager(config::kConfirmWindowMs);
  g_ready = false;
  fakeNow = 0;
  nextRead = {};
  lastMessage.clear();
  renders = polls = 0;
  readerOk = true;
}

static void tap(uint8_t tail = 0x56) {
  nextRead = {ReadStatus::Ok, {0x03, 0x10, 0x51, 0x70, 0x01, 0x01, 0x70, 0x12, 0x34, tail}};
  loop();
  nextRead = {};
}

int main() {
  resetDevice();
  setup();
  assert(g_ready && g_queue.size() == 0);
  const uint64_t salt = g_salt;
  tap();
  assert(g_queue.size() == 1 && lastMessage == "JOINED #1");
  tap();
  assert(g_queue.hasPending());
  fakeNow += 60000;
  loop();
  assert(g_queue.hasPending());
  nextRead.status = ReadStatus::Removed;
  loop();
  nextRead = {};
  fakeNow += 4999;
  tap();
  assert(g_queue.size() == 0 && lastMessage == "LEFT #1");

  // 加入后重启能用同一身份找到旧队列。
  tap();
  g_queue = QueueManager(config::kConfirmWindowMs);
  g_ready = false;
  setup();
  assert(g_ready && g_salt == salt && g_queue.size() == 1);

  // 长按过程中读卡仍可处理；按住再久只移除一次。
  fakeButton = LOW;
  loop();
  fakeNow += 1000;
  const int oldPolls = polls;
  tap(0x57);
  assert(polls == oldPolls + 1 && g_queue.size() == 1);
  loop();
  assert(g_queue.size() == 1);
  fakeButton = HIGH;
  loop();

  // 保存失败不能报告成功，恢复旧队列并停止处理后续读卡。
  Preferences::failWrite = true;
  const int oldRenders = renders;
  tap(0x58);
  assert(!g_ready && g_queue.size() == 1 && renders == oldRenders);
  assert(lastMessage == "STORAGE ERROR");
  const int stoppedPolls = polls;
  tap(0x59);
  assert(polls == stoppedPolls && g_queue.size() == 1);

  // 退出和按钮移除的保存失败也必须回退。
  resetDevice(); setup(); tap(); tap();
  nextRead.status = ReadStatus::Removed; loop();
  Preferences::failWrite = true;
  tap();
  assert(!g_ready && g_queue.size() == 1 && lastMessage == "STORAGE ERROR");
  resetDevice(); setup(); tap();
  Preferences::failWrite = true;
  fakeButton = LOW; loop(); fakeNow += 3000; loop();
  assert(!g_ready && g_queue.size() == 1 && lastMessage == "STORAGE ERROR");

  // 首次写盐失败、打开失败、损坏的队列和盐丢失都不能静默开始新队列。
  resetDevice(); Preferences::failWrite = true; setup();
  assert(!g_ready && lastMessage == "STORAGE ERROR");
  resetDevice(); Preferences::failOpen = true; setup();
  assert(!g_ready && lastMessage == "STORAGE ERROR");
  resetDevice(); setup(); tap();
  Preferences::data["queue"] = {0}; g_ready = false; setup();
  assert(!g_ready && lastMessage == "STORAGE ERROR");
  resetDevice(); setup(); tap();
  Preferences::data.erase("salt"); g_ready = false; setup();
  assert(!g_ready && !Preferences::data.count("salt"));
  resetDevice(); setup(); tap();
  Preferences::failRead = true; g_ready = false; setup();
  assert(!g_ready && lastMessage == "STORAGE ERROR");
  puts("device tests passed");
}
