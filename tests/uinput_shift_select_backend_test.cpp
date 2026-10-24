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
#include "uinput_shift_select_backend.h"

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
      : InputContext(manager, "shift-select-test") {}
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

void run(bool deliverAck, uint32_t count) {
  fcitx::EventLoop loop;
  fcitx::InputContextManager manager;
  Context context(manager), other(manager);
  FakeDevice device;
  areca::UinputShiftSelectBackend backend(loop, device, [] { return false; });
  std::unique_ptr<fcitx::EventSourceTime> ackTimer;
  uint64_t ackTime = 0;
  auto forward = [&](fcitx::KeySym sym, bool release, Context &ic) {
    fcitx::KeyEvent event(&ic, fcitx::Key(sym), release);
    const bool handled = backend.handleKeyEvent(event);
    assert(!event.accepted() && !event.filtered());
    return handled;
  };
  device.onKey = [&](uint16_t code, int value) {
    if (code == KEY_LEFT) {
      assert(forward(FcitxKey_Left, value == 0, context));
      // Lượt gửi lại không được lọc hay làm kết thúc phát Left sớm.
      assert(forward(FcitxKey_Left, value == 0, context));
    }
    if (code == KEY_LEFTSHIFT && value == 0 && deliverAck) {
      ackTimer = loop.addTimeEvent(
          CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 12000, 1,
          [&](fcitx::EventSourceTime *, uint64_t) {
            assert(context.commits.empty());
            assert(!forward(FcitxKey_Shift_L, true, other));
            assert(forward(FcitxKey_Shift_R, true, context) == false);
            ackTime = fcitx::now(CLOCK_MONOTONIC);
            assert(forward(FcitxKey_Shift_L, true, context));
            assert(forward(FcitxKey_Shift_L, true, context));
            assert(forward(FcitxKey_Left, false, context));
            assert(forward(FcitxKey_Left, true, context));
            return false;
          });
      ackTimer->setOneShot();
    }
  };
  areca::RewritePlan plan;
  plan.transactionId = 1;
  plan.backspaceCount = count;
  plan.uinputShiftSelectDelayMs = 2;
  plan.uinputShiftSelectAckTimeoutMs = 40;
  plan.ibusAfterUinputShiftSelectWaitMs = 10;
  plan.commitText = "x";
  int done = 0;
  assert(backend.apply(context, plan, [&](uint64_t, areca::RewriteOutcome outcome) {
    assert(outcome == areca::RewriteOutcome::Succeeded);
    ++done;
    loop.exit();
  }) == areca::ApplyStatus::Pending);
  // Shift release trước khi phát Shift up không được coi là ACK.
  assert(forward(FcitxKey_Shift_L, true, context));
  assert(context.commits.empty());
  auto watchdog = loop.addTimeEvent(
      CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 1000000, 1,
      [](fcitx::EventSourceTime *, uint64_t) { assert(false); return false; });
  loop.exec();
  assert(done == 1 && !backend.hasPending());
  assert(context.commits == "x");
  if (!count) {
    assert(device.keys.empty());
    return;
  }
  assert(device.keys.size() == count * 2 + 2);
  assert(device.keys.front().code == KEY_LEFTSHIFT && device.keys.front().value == 1);
  assert(device.keys.back().code == KEY_LEFTSHIFT && device.keys.back().value == 0);
  assert(device.keys[1].time >= device.keys.front().time + plan.uinputShiftSelectDelayMs * 1000);
  for (size_t i = 1; i + 1 < device.keys.size(); ++i) {
    assert(device.keys[i].code == KEY_LEFT);
    assert(device.keys[i].value == (i % 2 ? 1 : 0));
  }
  const auto upTime = device.keys.back().time;
  assert(upTime >= device.keys[device.keys.size() - 2].time +
                   plan.uinputShiftSelectDelayMs * 1000);
  if (deliverAck) {
    assert(context.commitTime >= ackTime + 10000);
  } else {
    assert(context.commitTime >= upTime + 49000);
  }
}
} // namespace
int main() {
  run(true, 3);
  run(false, 2);
  run(false, 0);
  std::cout << "Uinput Shift-select ACK tests passed\n";
}
