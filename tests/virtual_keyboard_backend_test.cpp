#include <cassert>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "virtual_keyboard_backend.h"
#include "virtual_keyboard_device.h"

namespace {

class TestInputContext final : public fcitx::InputContext {
public:
  TestInputContext(fcitx::InputContextManager &manager, const char *frontend)
      : fcitx::InputContext(manager, "test-app"), frontend_(frontend) {}
  ~TestInputContext() override { destroy(); }

  const char *frontend() const override { return frontend_; }
  void commitStringImpl(const std::string &text) override {
    commits.push_back(text);
  }
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}

  std::vector<std::string> commits;

private:
  const char *frontend_;
};

// Thiết bị giả: ghi lại yêu cầu, báo xong khi test gọi finish().
class FakeDevice final : public areca::VirtualKeyboardDevice {
public:
  FakeDevice(const char *kind, bool available)
      : kind_(kind), available_(available) {}

  const char *kind() const override { return kind_; }
  bool isAvailable() override { return available_; }
  void replace(const areca::VirtualKeyboardRequest &request,
               Done done) override {
    requests.push_back(request);
    pending = std::move(done);
  }
  void cancel() override {}

  void finish(bool ok) {
    auto done = std::move(pending);
    done(ok);
  }

  std::vector<areca::VirtualKeyboardRequest> requests;
  Done pending;

private:
  const char *kind_;
  bool available_;
};

areca::RewritePlan makePlan(uint64_t id, uint32_t backspaces,
                            std::string text) {
  areca::RewritePlan plan;
  plan.transactionId = id;
  plan.backspaceCount = backspaces;
  plan.commitText = std::move(text);
  plan.backspaceDelayMs = 2;
  return plan;
}

void testPureHelpers() {
  assert((areca::utf8Codepoints("gõ") == std::vector<uint32_t>{'g', 0xf5}));
  assert(areca::utf8Codepoints("\xff").empty());
  assert((areca::virtualKeyboardEchoSequence(2, "ơc") ==
          std::vector<uint32_t>{0, 0, 0x1a1, 'c'}));

  const std::string keymap = areca::buildVirtualKeyboardKeymap({0xf5, 'c'});
  // Backspace evdev 14 → XKB 22; ký tự đầu nằm ở phím evdev 2 → XKB 10.
  assert(keymap.find("<BKSP> = 22;") != std::string::npos);
  assert(keymap.find("key <BKSP> { [ BackSpace ] };") != std::string::npos);
  assert(keymap.find("<T0> = 10;") != std::string::npos);
  assert(keymap.find("key <T0> { [ U00F5 ] };") != std::string::npos);
  assert(keymap.find("key <T1> { [ U0063 ] };") != std::string::npos);
  assert(keymap.find("<T2>") == std::string::npos);

  // Không gán ký tự vào Backspace, Tab, Enter hay phím bổ trợ.
  for (size_t i = 0; i < areca::kVirtualKeyboardTextKeys; ++i) {
    const uint32_t code = areca::virtualKeyboardTextKeycode(i);
    assert(code != 14 && code != 15 && code != 28 && code != 29 && code != 42 &&
           code != 54 && code != 56);
  }
  // Chuỗi dài hơn số phím chỉ lấy phần đầu cho một lượt.
  const std::vector<uint32_t> many(areca::kVirtualKeyboardTextKeys + 5, 'a');
  const std::string capped = areca::buildVirtualKeyboardKeymap(many);
  assert(capped.find("<T" +
                     std::to_string(areca::kVirtualKeyboardTextKeys - 1) +
                     ">") != std::string::npos);
  assert(capped.find("<T" + std::to_string(areca::kVirtualKeyboardTextKeys) +
                     ">") == std::string::npos);
}

void testDeviceSelectionAndCompletion() {
  fcitx::EventLoop loop;
  fcitx::InputContextManager manager;
  TestInputContext waylandContext(manager, "wayland");
  TestInputContext ximContext(manager, "xim");

  // Phiên Wayland không có phím ảo: X11 chỉ dùng cho app X11 (frontend xim).
  setenv("WAYLAND_DISPLAY", "wayland-test", 1);
  setenv("XDG_SESSION_TYPE", "wayland", 1);
  auto wayland = std::make_unique<FakeDevice>("wayland", false);
  auto x11 = std::make_unique<FakeDevice>("x11", true);
  auto *x11Device = x11.get();
  areca::VirtualKeyboardBackend backend(
      loop, std::move(wayland), std::move(x11),
      [](std::function<void()> task) { task(); }, [] { return false; });

  assert(!backend.isAvailable(waylandContext));
  assert(backend.describe(waylandContext) == "không khả dụng");
  assert(backend.apply(waylandContext, makePlan(1, 1, "õ"), nullptr) ==
         areca::ApplyStatus::Failed);
  assert(backend.isAvailable(ximContext));
  assert(backend.describe(ximContext) == "X11");

  uint64_t doneId = 0;
  areca::RewriteOutcome outcome = areca::RewriteOutcome::Failed;
  const auto status =
      backend.apply(ximContext, makePlan(7, 1, "õ"),
                    [&](uint64_t id, areca::RewriteOutcome result) {
                      doneId = id;
                      outcome = result;
                    });
  assert(status == areca::ApplyStatus::Pending);
  assert(backend.hasPending());
  assert(x11Device->requests.size() == 1);
  assert(x11Device->requests[0].deleteCount == 1);
  assert(x11Device->requests[0].text == "õ");
  assert(x11Device->requests[0].keyIntervalMs == 2);
  // Đang có giao dịch thì không nhận giao dịch mới.
  assert(backend.apply(ximContext, makePlan(8, 1, "a"), nullptr) ==
         areca::ApplyStatus::Failed);

  x11Device->finish(true);
  assert(doneId == 7);
  assert(outcome == areca::RewriteOutcome::Succeeded);
  assert(!backend.hasPending());
  // Không bao giờ commitString: chữ đi bằng phím ảo.
  assert(ximContext.commits.empty());

  // Thiết bị báo hỏng thì giao dịch thất bại.
  assert(backend.apply(ximContext, makePlan(9, 0, "a"),
                       [&](uint64_t id, areca::RewriteOutcome result) {
                         doneId = id;
                         outcome = result;
                       }) == areca::ApplyStatus::Pending);
  x11Device->finish(false);
  assert(doneId == 9);
  assert(outcome == areca::RewriteOutcome::Failed);
}

void testWaylandPreferredWhenAvailable() {
  fcitx::EventLoop loop;
  fcitx::InputContextManager manager;
  TestInputContext context(manager, "wayland");
  areca::VirtualKeyboardBackend backend(
      loop, std::make_unique<FakeDevice>("wayland", true),
      std::make_unique<FakeDevice>("x11", true),
      [](std::function<void()> task) { task(); }, [] { return false; });
  assert(backend.describe(context) == "Wayland");
}

void testEchoKeysPassThrough() {
  fcitx::EventLoop loop;
  fcitx::InputContextManager manager;
  TestInputContext context(manager, "xim");
  TestInputContext other(manager, "xim");
  setenv("XDG_SESSION_TYPE", "x11", 1);
  auto x11 = std::make_unique<FakeDevice>("x11", true);
  auto *device = x11.get();
  areca::VirtualKeyboardBackend backend(
      loop, nullptr, std::move(x11), [](std::function<void()> task) { task(); },
      [] { return false; });

  assert(backend.apply(context, makePlan(3, 1, "õ"),
                       [](uint64_t, areca::RewriteOutcome) {}) ==
         areca::ApplyStatus::Pending);

  // Phím của ô khác không bị nhận.
  fcitx::KeyEvent foreign(&other, fcitx::Key(FcitxKey_BackSpace));
  assert(!backend.handleKeyEvent(foreign));

  // Phím người dùng không đúng thứ tự dội ngược: để Bamboo xử lý.
  fcitx::KeyEvent userKey(&context, fcitx::Key(FcitxKey_a));
  assert(!backend.handleKeyEvent(userKey));

  fcitx::KeyEvent backspace(&context, fcitx::Key(FcitxKey_BackSpace));
  assert(backend.handleKeyEvent(backspace));
  assert(!backspace.accepted());
  fcitx::KeyEvent backspaceUp(&context, fcitx::Key(FcitxKey_BackSpace), true);
  assert(backend.handleKeyEvent(backspaceUp));

  // Dội ngược đến sau khi thiết bị đã báo xong vẫn được nhận.
  device->finish(true);
  fcitx::KeyEvent text(&context,
                       fcitx::Key(fcitx::Key::keySymFromUnicode(0xf5)));
  assert(backend.handleKeyEvent(text));
  assert(!text.accepted());

  // Đã nhận đủ: phím sau đó là của người dùng.
  fcitx::KeyEvent again(&context, fcitx::Key(FcitxKey_BackSpace));
  assert(!backend.handleKeyEvent(again));
}

} // namespace

int main() {
  testPureHelpers();
  testDeviceSelectionAndCompletion();
  testWaylandPreferredWhenAvailable();
  testEchoKeysPassThrough();
  return 0;
}
