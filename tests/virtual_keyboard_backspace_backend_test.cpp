#include <cassert>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <linux/input-event-codes.h>

#include "virtual_keyboard_backspace_backend.h"
#include "virtual_keyboard_device.h"
#include "virtual_keyboard_keymap.h"

namespace {

class TestInputContext final : public fcitx::InputContext {
public:
  explicit TestInputContext(fcitx::InputContextManager &manager)
      : fcitx::InputContext(manager, "test-app") {}
  ~TestInputContext() override { destroy(); }

  const char *frontend() const override { return "wayland"; }
  void commitStringImpl(const std::string &text) override {
    commits.push_back(text);
  }
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}

  std::vector<std::string> commits;
};

// Thiết bị giả: ghi lại keymap và phím đã gửi, báo sync khi test gọi finish().
class FakeVirtualKeyboardDevice final : public areca::VirtualKeyboardDevice {
public:
  FakeVirtualKeyboardDevice(fcitx::EventLoop &loop, bool available)
      : areca::VirtualKeyboardDevice(loop, [] { return false; }),
        available_(available) {}

  bool isAvailable() override { return available_; }
  bool setKeymap(const std::string &keymap) override {
    keymaps.push_back(keymap);
    return true;
  }
  bool sendKey(uint32_t evdevKeycode, bool press) override {
    if (press) {
      pressed.push_back(evdevKeycode);
    }
    return true;
  }
  bool sync(SyncCallback callback) override {
    pendingSync = std::move(callback);
    return true;
  }
  void setLockedModifiers(uint32_t mask) override { locked.push_back(mask); }
  bool sendHeldModifiers(uint32_t mask) override {
    held.push_back(mask);
    return true;
  }

  void finish(bool ok) {
    auto done = std::move(pendingSync);
    assert(done);
    done(ok);
  }

  std::vector<std::string> keymaps;
  std::vector<uint32_t> pressed;
  std::vector<uint32_t> locked;
  std::vector<uint32_t> held;
  SyncCallback pendingSync;

private:
  bool available_;
};

areca::RewritePlan makePlan(uint64_t id, uint32_t backspaces,
                            std::string text) {
  areca::RewritePlan plan;
  plan.transactionId = id;
  plan.backspaceCount = backspaces;
  plan.commitText = std::move(text);
  return plan;
}

void testKeymapHelpers() {
  assert((areca::utf8Codepoints("gõ") == std::vector<uint32_t>{'g', 0xf5}));
  assert(areca::utf8Codepoints("\xff").empty());
  assert((areca::virtualKeyboardEchoSequence(2, "ơc") ==
          std::vector<uint32_t>{0, 0, 0x1a1, 'c'}));
  // Marker gõ trước các Backspace.
  assert((areca::virtualKeyboardEchoSequence(2, "ơ", areca::kRewriteMarker) ==
          std::vector<uint32_t>{0xb7, 0, 0, 0x1a1}));

  const std::string keymap = areca::buildVirtualKeyboardKeymap({0xf5, 'c'});
  assert(keymap.find("<BKSP> = 22;") != std::string::npos);
  assert(keymap.find("key <BKSP> { [ BackSpace ] };") != std::string::npos);
  // Ký tự đầu nằm trên phím A: evdev 30, XKB 38.
  assert(keymap.find("<C00> = 38;") != std::string::npos);
  assert(
      keymap.find("key <C00> { type = \"ARECA_CARRIER\", [ 0x10000f5 ] };") !=
      std::string::npos);
  assert(
      keymap.find("key <C01> { type = \"ARECA_CARRIER\", [ 0x1000063 ] };") !=
      std::string::npos);
  // Phím chở chữ dùng bit Lock để CapsLock đi kèm không đổi chữ hoa.
  assert(keymap.find("type \"ARECA_CARRIER\" {") != std::string::npos);
  assert(keymap.find("<C02>") == std::string::npos);
  // Đủ phím bổ trợ và tên đèn để bit khoá, phím đang giữ có nghĩa.
  assert(keymap.find("modifier_map Mod2 { <NMLK> };") != std::string::npos);
  assert(keymap.find("modifier_map Shift { <LFSH> };") != std::string::npos);
  assert(keymap.find("modifier_map Mod1 { <LALT> };") != std::string::npos);
  assert(keymap.find("indicator 2 = \"Num Lock\";") != std::string::npos);

  // Phím chở ký tự không bao giờ là Backspace, Tab, Enter hay phím bổ trợ.
  for (size_t slot = 0; slot < areca::kVirtualKeyboardCarrierKeys; ++slot) {
    const uint32_t code = areca::virtualKeyboardCarrierKeycode(slot);
    assert(code != KEY_BACKSPACE && code != KEY_TAB && code != KEY_ENTER &&
           code != KEY_LEFTCTRL && code != KEY_LEFTSHIFT &&
           code != KEY_RIGHTSHIFT && code != KEY_LEFTALT);
  }

  // Chỉ xoá vẫn có một đợt rỗng; chuỗi dài được chia đều theo số phím.
  assert(areca::splitVirtualKeyboardBursts({}).size() == 1);
  assert(areca::splitVirtualKeyboardBursts({}).front().empty());
  const std::vector<uint32_t> many(areca::kVirtualKeyboardCarrierKeys + 4, 'a');
  const auto bursts = areca::splitVirtualKeyboardBursts(many);
  assert(bursts.size() == 2);
  assert(bursts[0].size() == areca::kVirtualKeyboardCarrierKeys);
  assert(bursts[1].size() == 4);
}

void testUnavailableDevice() {
  fcitx::EventLoop loop;
  fcitx::InputContextManager manager;
  TestInputContext context(manager);
  FakeVirtualKeyboardDevice device(loop, false);
  areca::VirtualKeyboardBackspaceBackend backend(loop, device,
                                                 [] { return false; });
  // Compositor không cấp phím ảo (KWin, GNOME): không nhận giao dịch để bộ
  // chọn Backspace dùng uinput.
  assert(!backend.isAvailable());
  assert(backend.apply(context, makePlan(1, 1, "õ"), nullptr) ==
         areca::ApplyStatus::Failed);
  assert(device.keymaps.empty());
}

void testDeletesThenTypesWithoutCommit() {
  fcitx::EventLoop loop;
  fcitx::InputContextManager manager;
  TestInputContext context(manager);
  FakeVirtualKeyboardDevice device(loop, true);
  areca::VirtualKeyboardBackspaceBackend backend(loop, device,
                                                 [] { return false; });

  uint64_t doneId = 0;
  areca::RewriteOutcome outcome = areca::RewriteOutcome::Failed;
  const auto onDone = [&](uint64_t id, areca::RewriteOutcome result) {
    doneId = id;
    outcome = result;
  };
  assert(backend.apply(context, makePlan(7, 2, "õc"), onDone) ==
         areca::ApplyStatus::Pending);
  assert(backend.hasPending());
  assert(device.keymaps.size() == 1);
  // Backspace trước, rồi phím chở từng ký tự theo thứ tự.
  assert((device.pressed ==
          std::vector<uint32_t>{KEY_BACKSPACE, KEY_BACKSPACE,
                                areca::virtualKeyboardCarrierKeycode(0),
                                areca::virtualKeyboardCarrierKeycode(1)}));
  // Đang có giao dịch thì không nhận giao dịch mới.
  assert(backend.apply(context, makePlan(8, 1, "a"), nullptr) ==
         areca::ApplyStatus::Failed);

  device.finish(true);
  assert(doneId == 7);
  assert(outcome == areca::RewriteOutcome::Succeeded);
  assert(!backend.hasPending());
  assert(context.commits.empty());
}

void testTypedMarkerGoesFirst() {
  fcitx::EventLoop loop;
  fcitx::InputContextManager manager;
  TestInputContext context(manager);
  FakeVirtualKeyboardDevice device(loop, true);
  areca::VirtualKeyboardBackspaceBackend backend(loop, device,
                                                 [] { return false; });

  auto plan = makePlan(4, 2, "ê");
  plan.typeMarker = true;
  bool done = false;
  assert(backend.apply(context, plan, [&](uint64_t, areca::RewriteOutcome) {
    done = true;
  }) == areca::ApplyStatus::Pending);
  // Đợt đầu chỉ có marker: gõ marker rồi xoá cả marker lẫn ký tự cũ.
  assert(device.keymaps.size() == 1);
  assert(device.keymaps.front().find("0x10000b7") != std::string::npos);
  assert((device.pressed ==
          std::vector<uint32_t>{areca::virtualKeyboardCarrierKeycode(0),
                                KEY_BACKSPACE, KEY_BACKSPACE}));
  device.finish(true);
  assert(!done);
  // Đợt sau gõ chữ mới, không còn Backspace.
  assert(device.keymaps.size() == 2);
  assert(device.pressed.back() == areca::virtualKeyboardCarrierKeycode(0));
  assert(device.pressed.size() == 4);
  device.finish(true);
  assert(done);
  assert(context.commits.empty());

  // Marker gõ lại từ phím ảo được nhận như phím của chính Areca.
  fcitx::KeyEvent markerEcho(&context,
                             fcitx::Key(fcitx::Key::keySymFromUnicode(0xb7)));
  assert(backend.handleKeyEvent(markerEcho));
}

void testKeepsLockAndHeldModifiers() {
  fcitx::EventLoop loop;
  fcitx::InputContextManager manager;
  TestInputContext context(manager);
  FakeVirtualKeyboardDevice device(loop, true);
  uint32_t state = static_cast<uint32_t>(
      fcitx::KeyStates{fcitx::KeyState::NumLock, fcitx::KeyState::Shift});
  areca::VirtualKeyboardBackspaceBackend backend(
      loop, device, [] { return false; }, [&state] { return state; });

  auto plan = makePlan(6, 2, "ê");
  plan.typeMarker = true;
  bool done = false;
  assert(!backend.expectsEcho());
  assert(backend.apply(context, plan, [&](uint64_t, areca::RewriteOutcome) {
    done = true;
  }) == areca::ApplyStatus::Pending);
  // Engine bỏ qua cờ phím bổ trợ của phím dội về trong khoảng này.
  assert(backend.expectsEcho());
  // Chỉ bit khoá đi kèm keymap; Shift đang giữ không được kèm phím xoá, gõ.
  assert((device.locked == std::vector<uint32_t>{static_cast<uint32_t>(
                               fcitx::KeyState::NumLock)}));
  // Đợt marker chưa phải đợt cuối nên chưa trả phím bổ trợ.
  assert(device.held.empty());
  // Người dùng nhả Shift trong lúc phím ảo đang gõ.
  state = static_cast<uint32_t>(fcitx::KeyState::NumLock);
  device.finish(true);
  assert((device.held == std::vector<uint32_t>{0}));
  device.finish(true);
  assert(done);

  // Còn giữ Shift khi gõ xong thì trả lại đúng Shift.
  state = static_cast<uint32_t>(
      fcitx::KeyStates{fcitx::KeyState::NumLock, fcitx::KeyState::Shift});
  device.held.clear();
  done = false;
  assert(backend.apply(context, makePlan(7, 1, "a"),
                       [&](uint64_t, areca::RewriteOutcome) { done = true; }) ==
         areca::ApplyStatus::Pending);
  assert((device.held == std::vector<uint32_t>{
                             static_cast<uint32_t>(fcitx::KeyState::Shift)}));
  device.finish(true);
  assert(done);
}

void testLongTextUsesSeveralBursts() {
  fcitx::EventLoop loop;
  fcitx::InputContextManager manager;
  TestInputContext context(manager);
  FakeVirtualKeyboardDevice device(loop, true);
  areca::VirtualKeyboardBackspaceBackend backend(loop, device,
                                                 [] { return false; });

  const std::string text(areca::kVirtualKeyboardCarrierKeys + 2, 'a');
  bool done = false;
  assert(backend.apply(context, makePlan(5, 1, text),
                       [&](uint64_t, areca::RewriteOutcome) { done = true; }) ==
         areca::ApplyStatus::Pending);
  assert(device.keymaps.size() == 1);
  // Đợt sau chỉ gửi khi compositor đã nhận hết đợt trước.
  device.finish(true);
  assert(!done);
  assert(device.keymaps.size() == 2);
  device.finish(true);
  assert(done);
  // Backspace chỉ có ở đợt đầu.
  size_t backspaces = 0;
  for (const auto code : device.pressed) {
    backspaces += code == KEY_BACKSPACE;
  }
  assert(backspaces == 1);
  assert(device.pressed.size() == text.size() + 1);
}

void testSyncFailureFailsTransaction() {
  fcitx::EventLoop loop;
  fcitx::InputContextManager manager;
  TestInputContext context(manager);
  FakeVirtualKeyboardDevice device(loop, true);
  areca::VirtualKeyboardBackspaceBackend backend(loop, device,
                                                 [] { return false; });

  areca::RewriteOutcome outcome = areca::RewriteOutcome::Succeeded;
  assert(backend.apply(context, makePlan(9, 0, "a"),
                       [&](uint64_t, areca::RewriteOutcome result) {
                         outcome = result;
                       }) == areca::ApplyStatus::Pending);
  device.finish(false);
  assert(outcome == areca::RewriteOutcome::Failed);
  assert(!backend.hasPending());
}

void testEchoKeysPassThrough() {
  fcitx::EventLoop loop;
  fcitx::InputContextManager manager;
  TestInputContext context(manager);
  TestInputContext other(manager);
  FakeVirtualKeyboardDevice device(loop, true);
  areca::VirtualKeyboardBackspaceBackend backend(loop, device,
                                                 [] { return false; });

  assert(backend.apply(context, makePlan(3, 1, "õ"),
                       [](uint64_t, areca::RewriteOutcome) {}) ==
         areca::ApplyStatus::Pending);

  // Phím của ô khác không bị nhận.
  fcitx::KeyEvent foreign(&other, fcitx::Key(FcitxKey_BackSpace));
  assert(!backend.handleKeyEvent(foreign));

  // Phím người dùng không đúng thứ tự dội về: để Bamboo xử lý.
  fcitx::KeyEvent userKey(&context, fcitx::Key(FcitxKey_a));
  assert(!backend.handleKeyEvent(userKey));

  fcitx::KeyEvent backspace(&context, fcitx::Key(FcitxKey_BackSpace));
  assert(backend.handleKeyEvent(backspace));
  fcitx::KeyEvent backspaceUp(&context, fcitx::Key(FcitxKey_BackSpace), true);
  assert(backend.handleKeyEvent(backspaceUp));

  // Phím dội về sau khi đã báo xong vẫn được nhận.
  device.finish(true);
  fcitx::KeyEvent text(&context,
                       fcitx::Key(fcitx::Key::keySymFromUnicode(0xf5)));
  assert(backend.handleKeyEvent(text));

  // Đã nhận đủ: phím sau đó là của người dùng.
  fcitx::KeyEvent again(&context, fcitx::Key(FcitxKey_BackSpace));
  assert(!backend.handleKeyEvent(again));
}

} // namespace

int main() {
  testKeymapHelpers();
  testUnavailableDevice();
  testDeletesThenTypesWithoutCommit();
  testTypedMarkerGoesFirst();
  testKeepsLockAndHeldModifiers();
  testLongTextUsesSeveralBursts();
  testSyncFailureFailsTransaction();
  testEchoKeysPassThrough();
  std::cout << "VirtualKeyboardBackspaceBackend test passed successfully\n";
  return 0;
}
