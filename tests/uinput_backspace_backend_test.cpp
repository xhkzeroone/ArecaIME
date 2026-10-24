#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include <linux/input.h>

#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "uinput_device.h"
#include "uinput_backspace_backend.h"

namespace {
struct SentKey { uint16_t code; int value; uint64_t time; };
class FakeDevice final : public areca::UinputDevice {
public:
  FakeDevice() : UinputDevice([] { return false; }) {}
  bool ensureDevice() override { return true; }
  void sendKeyEvent(uint16_t code, int value) override {
    keys.push_back({code, value, fcitx::now(CLOCK_MONOTONIC)});
    if (onKey) onKey(code, value);
  }
  std::vector<SentKey> keys;
  std::function<void(uint16_t, int)> onKey;
};
class Context final : public fcitx::InputContext {
public:
  explicit Context(fcitx::InputContextManager &manager)
      : InputContext(manager, "backspace-test") {}
  ~Context() override { destroy(); }
  const char *frontend() const override { return "ibus"; }
  void commitStringImpl(const std::string &text) override {
    commits += text;
    commitTime = fcitx::now(CLOCK_MONOTONIC);
  }
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}
  std::string commits;
  uint64_t commitTime = 0;
};


void run(bool earlyAck, uint32_t waitMs, uint32_t count) {
  fcitx::EventLoop loop;
  fcitx::InputContextManager manager;
  Context context(manager);
  FakeDevice device;
  areca::AdaptiveWait adaptive;
  areca::UinputBackspaceBackend backend(loop, device, adaptive, [] { return false; });
  areca::RewritePlan plan;
  plan.transactionId = 1;
  plan.backspaceCount = count;
  plan.backspaceDelayMs = 20;
  plan.uinputBackspaceAckTimeoutMs = 40;
  plan.ibusAfterBackspaceWaitMs = waitMs;
  plan.commitText = "X";
  int completed = 0;
  uint64_t ackTime = 0;
  auto deliver = [&](bool release, bool repeat) {
    fcitx::KeyEvent event(&context,
        fcitx::Key(FcitxKey_BackSpace, repeat ? fcitx::KeyState::Repeat
                                            : fcitx::KeyState::NoState, 22),
        release, 1234);
    assert(backend.handleBackspace(event));
    assert(!event.filtered() && !event.accepted());
  };
  assert(backend.apply(context, plan, [&](uint64_t id, areca::RewriteOutcome result) {
    assert(id == 1 && result == areca::RewriteOutcome::Succeeded);
    ++completed;
    loop.exit();
  }) == areca::ApplyStatus::Pending);
  std::unique_ptr<fcitx::EventSourceTime> ackTimer;
  if (earlyAck) {
    // Đã phát một Backspace, nhưng hai press cùng time/code làm đủ ngưỡng.
    // Timer gửi phím thứ hai phải tiếp tục dù timer commit đã bắt đầu.
    ackTimer = loop.addTimeEvent(CLOCK_MONOTONIC,
        fcitx::now(CLOCK_MONOTONIC) + 1000, 1,
        [&](fcitx::EventSourceTime *, uint64_t) {
          assert(device.keys.size() == 2);
          ackTime = fcitx::now(CLOCK_MONOTONIC);
          deliver(false, false);
          deliver(false, true);
          deliver(true, false);
          deliver(true, false);
          // Event sau ngưỡng vẫn forward và không mở thêm lần commit.
          deliver(false, true);
          return false;
        });
    ackTimer->setOneShot();
  }
  auto watchdog = loop.addTimeEvent(CLOCK_MONOTONIC,
      fcitx::now(CLOCK_MONOTONIC) + 2000000, 1,
      [](fcitx::EventSourceTime *, uint64_t) { assert(false); return false; });
  loop.exec();
  assert(completed == 1 && !backend.hasPending());
  assert(context.commits == "X");
  assert(device.keys.size() == count * 2);
  for (size_t i = 0; i < device.keys.size(); ++i) {
    assert(device.keys[i].code == KEY_BACKSPACE);
    assert(device.keys[i].value == (i % 2 == 0 ? 1 : 0));
  }
  if (count) {
    assert(context.commitTime >= device.keys.back().time);
    if (earlyAck) {
      assert(context.commitTime >= ackTime + waitMs * 1000);
    } else {
      assert(context.commitTime >= device.keys.back().time + 40000);
    }
  }
}
} // namespace
int main() {
  run(true, 60, 2);  // Delay đủ rộng: tiếp tục gửi trong lúc đang chờ commit.
  run(true, 1, 2);   // Delay quá ngắn: không commit trước khi phát hết phím.
  run(false, 1, 2);  // Thiếu ACK: timeout vẫn hoàn tất một lần.
  run(false, 1, 0);  // Không cần xóa: commit trực tiếp sau delay.
  std::cout << "Uinput Backspace timer tests passed\n";
}
