#include <cassert>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "areca_config.h"
#include "input_scheduler.h"
#include "rewrite_mode.h"

namespace {

class TestInputContext final : public fcitx::InputContext {
public:
  explicit TestInputContext(fcitx::InputContextManager &manager,
                            const char *program = "google-chrome",
                            const char *frontend = "wayland")
      : fcitx::InputContext(manager, program), frontend_(frontend) {}
  ~TestInputContext() override { destroy(); }

  const char *frontend() const override { return frontend_; }

  void commitStringImpl(const std::string &text) override {
    events.push_back("commit:" + text);
  }
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &event) override {
    events.push_back(std::string(event.isRelease() ? "up:" : "down:") +
                     fcitx::Key::keySymToUTF8(event.rawKey().sym()));
  }
  void updatePreeditImpl() override {}

  std::vector<std::string> events;

private:
  const char *frontend_;
};

class TestEngine final : public areca::VietnameseEngine {
public:
  bool canProcessKey(uint32_t codepoint) const override {
    return codepoint != ' ' && codepoint != '.';
  }

  areca::BambooResult process(uint32_t, const std::string &text) override {
    areca::BambooResult result;
    result.currentText = current_;
    result.newText = current_ + text;
    result.deleteCount = text == "r" ? 1 : 0;
    result.commitText = text == "x" ? "X" : text;
    current_ = result.newText;
    return result;
  }

  areca::BambooResult processWithoutMacro(
      uint32_t codepoint, const std::string &text) override {
    ++withoutMacroProcessCount;
    return process(codepoint, text);
  }

  areca::BambooResult processBackspace() override {
    ++backspaceProcessCount;
    macroUndoAvailable = false;
    return {};
  }
  void backspace() override {
    ++plainBackspaceCount;
  }
  void reset() override {
    current_.clear();
  }
  bool restoreFromRenderedText(const std::string &text) override {
    if (text.empty() || text == "tiế" || text == "iế") {
      return false;
    }
    current_ = text;
    return true;
  }
  const std::string &currentText() const override { return current_; }
  bool canUndoMacro() const override { return macroUndoAvailable; }

  int backspaceProcessCount = 0;
  int plainBackspaceCount = 0;
  int withoutMacroProcessCount = 0;
  bool macroUndoAvailable = false;

private:
  std::string current_;
};

} // namespace

int main() {
  // Macro undo phải chặn cứng auto-repeat và chỉ xử lý đúng một lần khi thả
  // Backspace, giống BackspaceRecovery thường.
  // GNOME/IBus gửi các press lặp không kèm cờ Repeat.
  for (bool ibusWithoutRepeatFlag : {false, true}) {
    const auto repeatState = ibusWithoutRepeatFlag
                                 ? fcitx::KeyState::NoState
                                 : fcitx::KeyState::Repeat;
    fcitx::EventLoop loop;
    fcitx::InputContextManager manager;
    areca::RewriteModeHandler::StateFactory factory(
        [](fcitx::InputContext &) {
          auto *state = new areca::RewriteInputState(
              "Telex 2", true, true, true, "Unicode", false, false, 1, {});
          auto engine = std::make_unique<TestEngine>();
          engine->macroUndoAvailable = true;
          state->engine = std::move(engine);
          return state;
        });
    manager.registerProperty("rewrite-macro-backspace-test", &factory);
    TestInputContext context(manager, "org.gnome.TextEditor",
                             ibusWithoutRepeatFlag ? "ibus" : "wayland");
    areca::InputScheduler scheduler(
        loop,
        [&](fcitx::InputContext &ic) {
          return ic.propertyFor(&factory)->engine.get();
        },
        [](fcitx::InputContext &) {
          areca::SchedulerTiming timing;
          timing.postCommitDelayMs = 0;
          return timing;
        },
        [] { return false; },
        [](fcitx::InputContext &, const areca::BambooResult &) {
          return areca::RewriteBackendSelection{};
        });
    areca::RewriteModeHandler handler(
        loop, factory, scheduler, [] { return false; }, [] { return false; },
        [](fcitx::InputContext &, const char *) {}, [] { return true; },
        [](fcitx::InputContext &) { return false; }, [] { return false; },
        [](const fcitx::Key &key) {
          return key.normalize().sym() == FcitxKey_space &&
                 key.states().test(fcitx::KeyState::Shift);
        });

    auto *state = context.propertyFor(&factory);
    auto *engine = static_cast<TestEngine *>(state->engine.get());

    fcitx::KeyEvent press(&context, fcitx::Key(FcitxKey_BackSpace));
    handler.handleKeyEvent(press);
    assert(press.accepted());
    assert(state->backspaceRecoveryAwaitingRelease);
    assert(engine->backspaceProcessCount == 0);

    fcitx::KeyEvent repeat(
        &context,
        fcitx::Key(FcitxKey_BackSpace, repeatState));
    handler.handleKeyEvent(repeat);
    assert(!repeat.accepted());
    assert(!state->backspaceRecoveryAwaitingRelease);
    assert(engine->backspaceProcessCount == 0);
    assert(engine->currentText().empty());

    fcitx::KeyEvent release(&context, fcitx::Key(FcitxKey_BackSpace), true);
    handler.handleKeyEvent(release);
    assert(!release.accepted());
    assert(!state->backspaceRecoveryAwaitingRelease);
    assert(engine->backspaceProcessCount == 0);

    // Đợi qua cửa sổ bảo vệ phím inject sau transaction trước đó.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Giống Preedit: hễ còn composition thì Backspace luôn đi qua
    // processBackspace(), không phụ thuộc cờ trạng thái spellcheck riêng.
    engine->process('a', "a");
    fcitx::KeyEvent plainPress(&context, fcitx::Key(FcitxKey_BackSpace));
    handler.handleKeyEvent(plainPress);
    assert(plainPress.accepted());
    assert(state->backspaceRecoveryAwaitingRelease);

    fcitx::KeyEvent plainRelease(&context, fcitx::Key(FcitxKey_BackSpace), true);
    handler.handleKeyEvent(plainRelease);
    assert(plainRelease.accepted());
    assert(!state->backspaceRecoveryAwaitingRelease);
    assert(engine->backspaceProcessCount == 1);
    assert(engine->plainBackspaceCount == 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Lần Backspace tiếp theo vẫn đi cùng luồng; không có cờ one-shot làm nó
    // rơi sang nhánh forward.
    engine->process('a', "a");
    fcitx::KeyEvent restoredPress(&context, fcitx::Key(FcitxKey_BackSpace));
    handler.handleKeyEvent(restoredPress);
    assert(restoredPress.accepted());
    assert(state->backspaceRecoveryAwaitingRelease);

    fcitx::KeyEvent restoredRepeat(
        &context,
        fcitx::Key(FcitxKey_BackSpace, repeatState));
    handler.handleKeyEvent(restoredRepeat);
    assert(!restoredRepeat.accepted());
    assert(!state->backspaceRecoveryAwaitingRelease);
    assert(engine->currentText().empty());

    fcitx::KeyEvent continuedHold(
        &context, fcitx::Key(FcitxKey_BackSpace, repeatState));
    handler.handleKeyEvent(continuedHold);
    assert(!continuedHold.accepted());
    assert(!state->backspaceRecoveryAwaitingRelease);
    assert(engine->backspaceProcessCount == 1);

    // Sau khi thấy repeat, mọi event còn lại đi thẳng xuống ứng dụng.
    fcitx::KeyEvent repeatedRelease(
        &context,
        fcitx::Key(FcitxKey_BackSpace, repeatState), true);
    handler.handleKeyEvent(repeatedRelease);
    assert(!repeatedRelease.accepted());
    assert(!state->backspaceRecoveryAwaitingRelease);
    assert(engine->backspaceProcessCount == 1);

    fcitx::KeyEvent repeatAfterRelease(
        &context,
        fcitx::Key(FcitxKey_BackSpace, repeatState));
    handler.handleKeyEvent(repeatAfterRelease);
    assert(!repeatAfterRelease.accepted());
    assert(!state->backspaceRecoveryAwaitingRelease);

    fcitx::KeyEvent restoredRelease(&context,
                                    fcitx::Key(FcitxKey_BackSpace), true);
    handler.handleKeyEvent(restoredRelease);
    assert(!restoredRelease.accepted());
    assert(!state->backspaceRecoveryAwaitingRelease);
    assert(engine->backspaceProcessCount == 1);

    engine->reset();
    engine->process('b', "b");
    engine->process('t', "t");
    fcitx::KeyEvent bypassSpace(
        &context, fcitx::Key(FcitxKey_space, fcitx::KeyState::Shift));
    handler.handleKeyEvent(bypassSpace);
    assert(bypassSpace.accepted());
    assert(engine->withoutMacroProcessCount == 1);
  }

  // So sánh handler với luồng accept/enqueue cũ, dùng cùng loại engine giả.
  for (bool initiallyEnabled : {false, true}) {
    fcitx::EventLoop loop;
    fcitx::InputContextManager manager;
    areca::RewriteModeHandler::StateFactory factory([](fcitx::InputContext &) {
      auto *state = new areca::RewriteInputState(
          "Telex 2", true, true, true, "Unicode", false, false, 1, {});
      state->engine = std::make_unique<TestEngine>();
      return state;
    });
    manager.registerProperty("rewrite-test", &factory);
    TestInputContext context(manager);
    bool enabled = initiallyEnabled;
    areca::InputScheduler scheduler(loop,
        [&](fcitx::InputContext &ic) { return ic.propertyFor(&factory)->engine.get(); },
        [](fcitx::InputContext &) { areca::SchedulerTiming timing; timing.postCommitDelayMs = 0; return timing; },
        [] { return false; },
        [](fcitx::InputContext &, const areca::BambooResult &) {
          return areca::RewriteBackendSelection{};
        });
    areca::RewriteModeHandler handler(loop, factory, scheduler,
        [] { return false; }, [] { return false; },
        [](fcitx::InputContext &, const char *) {}, [] { return false; },
        [&](fcitx::InputContext &ic) {
          return enabled && ic.program() == "google-chrome";
        },
        [] { return false; }, [](const fcitx::Key &) { return false; });
    if (enabled) {
      fcitx::KeyEvent first(&context, fcitx::Key(FcitxKey_a));
      handler.handleKeyEvent(first);
      assert(!first.accepted());
      assert(context.events.empty());
      // Tắt ngay trên handler đang tồn tại; giả lập composition đã được xóa.
      enabled = false;
      context.propertyFor(&factory)->engine->reset();
    }
    fcitx::KeyEvent key(&context, fcitx::Key(FcitxKey_a));
    handler.handleKeyEvent(key);
    assert(key.accepted());
    assert(context.events == std::vector<std::string>{"commit:a"});
    assert(context.propertyFor(&factory)->engine->currentText() == "a");
    assert(scheduler.queuedKeyCount() == 0);

    // Luồng cũ phải tạo cùng kết quả và xử lý Bamboo đúng một lần.
    TestInputContext legacyContext(manager);
    TestEngine legacyEngine;
    areca::InputScheduler legacy(loop,
        [&](fcitx::InputContext &) -> areca::VietnameseEngine * { return &legacyEngine; },
        [](fcitx::InputContext &) { areca::SchedulerTiming timing; timing.postCommitDelayMs = 0; return timing; },
        [] { return false; },
        [](fcitx::InputContext &, const areca::BambooResult &) {
          return areca::RewriteBackendSelection{};
        });
    fcitx::KeyEvent legacyKey(&legacyContext, fcitx::Key(FcitxKey_a));
    legacyKey.filterAndAccept();
    legacy.enqueue(legacyContext, 'a', "a");
    assert(context.events == legacyContext.events);
    assert(key.accepted() == legacyKey.accepted());
    assert(context.propertyFor(&factory)->engine->currentText() == legacyEngine.currentText());

    // Bật option không được forward phím đầu trong ứng dụng ngoài trình duyệt.
    enabled = true;
    TestInputContext nonBrowserContext(manager, "org.gnome.TextEditor");
    fcitx::KeyEvent nonBrowserKey(&nonBrowserContext,
                                  fcitx::Key(FcitxKey_a));
    handler.handleKeyEvent(nonBrowserKey);
    assert(nonBrowserKey.accepted());
    assert(nonBrowserContext.events ==
           std::vector<std::string>{"commit:a"});
  }

  // Restore được gọi đúng một lần trước phím text đầu tiên và phần text đã
  // commit trở thành prefix của composition, nên chỉ ký tự mới được commit.
  fcitx::EventLoop restoreLoop;
  fcitx::InputContextManager restoreManager;
  areca::RewriteModeHandler::StateFactory restoreFactory(
      [](fcitx::InputContext &) {
        auto *state = new areca::RewriteInputState(
            "Telex 2", true, true, true, "Unicode", false, false, 1, {});
        state->engine = std::make_unique<TestEngine>();
        return state;
      });
  restoreManager.registerProperty("rewrite-restore-test", &restoreFactory);
  TestInputContext restoreContext(restoreManager, "org.gnome.TextEditor");
  restoreContext.setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
  // Caret nằm giữa từ: Rewrite chỉ phục hồi prefix trước caret; suffix `n`
  // tiếp tục nằm trong ứng dụng và không tham gia transaction xóa/chèn.
  restoreContext.surroundingText().setText("tiến", 3, 3);
  areca::InputScheduler restoreScheduler(
      restoreLoop,
      [&](fcitx::InputContext &ic) {
        return ic.propertyFor(&restoreFactory)->engine.get();
      },
      [](fcitx::InputContext &) {
        areca::SchedulerTiming timing;
        timing.postCommitDelayMs = 0;
        return timing;
      },
      [] { return false; },
      [](fcitx::InputContext &, const areca::BambooResult &) {
        return areca::RewriteBackendSelection{};
      });
  areca::RewriteModeHandler restoreHandler(
      restoreLoop, restoreFactory, restoreScheduler, [] { return false; },
      [] { return false; }, [](fcitx::InputContext &, const char *) {},
      [] { return false; }, [](fcitx::InputContext &) { return false; },
      [] { return true; }, [](const fcitx::Key &) { return false; });
  restoreHandler.activate(restoreContext);
  fcitx::KeyEvent restoreKey(&restoreContext, fcitx::Key(FcitxKey_a));
  restoreHandler.handleKeyEvent(restoreKey);
  assert(restoreKey.accepted());
  assert(restoreContext.events == std::vector<std::string>{"commit:a"});
  assert(restoreContext.propertyFor(&restoreFactory)->engine->currentText() ==
         "ếa");

  fcitx::InputContextManager boundaryManager;
  areca::RewriteModeHandler::StateFactory boundaryFactory(
      [](fcitx::InputContext &) {
        auto *state = new areca::RewriteInputState(
            "Telex 2", true, true, true, "Unicode", false, false, 1, {});
        state->engine = std::make_unique<TestEngine>();
        return state;
      });
  boundaryManager.registerProperty("rewrite-boundary-test", &boundaryFactory);
  TestInputContext boundaryContext(boundaryManager, "org.gnome.TextEditor");
  boundaryContext.setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
  // Mô phỏng snapshot cũ sau khi macro `k → không` đã được commit bởi dấu
  // chấm nhưng frontend chưa kịp trả snapshot chứa dấu chấm.
  boundaryContext.surroundingText().setText("không", 5, 5);
  areca::InputScheduler boundaryScheduler(
      restoreLoop,
      [&](fcitx::InputContext &ic) {
        return ic.propertyFor(&boundaryFactory)->engine.get();
      },
      [](fcitx::InputContext &) {
        areca::SchedulerTiming timing;
        timing.postCommitDelayMs = 0;
        return timing;
      },
      [] { return false; },
      [](fcitx::InputContext &, const areca::BambooResult &) {
        return areca::RewriteBackendSelection{};
      });
  areca::RewriteModeHandler boundaryHandler(
      restoreLoop, boundaryFactory, boundaryScheduler, [] { return false; },
      [] { return false; }, [](fcitx::InputContext &, const char *) {},
      [] { return false; }, [](fcitx::InputContext &) { return false; },
      [] { return true; }, [](const fcitx::Key &) { return false; });
  boundaryHandler.activate(boundaryContext);
  auto *boundaryState = boundaryContext.propertyFor(&boundaryFactory);
  assert(boundaryState->wordSerial == 0);
  assert(!boundaryState->wordBoundaryPending);
  fcitx::KeyEvent trailingSpace(&boundaryContext, fcitx::Key(FcitxKey_space));
  boundaryHandler.handleKeyEvent(trailingSpace);
  assert(!boundaryState->surroundingRestoreArmed);
  assert(boundaryState->wordSerial == 0);
  assert(boundaryState->wordBoundaryPending);
  fcitx::KeyEvent nextLetter(&boundaryContext, fcitx::Key(FcitxKey_a));
  boundaryHandler.handleKeyEvent(nextLetter);
  assert(boundaryState->wordSerial == 1);
  assert(!boundaryState->wordBoundaryPending);
  assert((boundaryContext.events ==
          std::vector<std::string>{"commit: ", "commit:a"}));
  assert(boundaryContext.propertyFor(&boundaryFactory)->engine->currentText() ==
         " a");

  areca::AppProfilesConfig appProfiles;
  assert(appProfiles.appOverrides.value().empty());
  areca::AppBackendOverrideEntry overrideEntry;
  overrideEntry.appName.setValue("discord");
  overrideEntry.mode.setValue(areca::AppBackendMode::ShiftSelect);
  appProfiles.appOverrides.mutableValue()->push_back(std::move(overrideEntry));
  assert(appProfiles.appOverrides.value().size() == 1);
  assert(appProfiles.appOverrides.value()[0].appName.value() == "discord");
  assert(appProfiles.appOverrides.value()[0].mode.value() ==
         areca::AppBackendMode::ShiftSelect);

  areca::AdvancedConfig advancedConfig;
  assert(!advancedConfig.useSurroundingV2InsteadOfSurroundingV1.value());
  assert(advancedConfig.useUinputInsteadOfForwardKey.value());
  assert(!advancedConfig.useXTestInsteadOfUinput.value());
  assert(advancedConfig.surroundingProbeTimeoutMs.value() == 80);

  areca::ArecaConfig mainConfig;
  assert(mainConfig.macroBypassModifier.value() ==
         areca::MacroBypassModifier::Shift);
  assert(areca::isMacroBypassSpace(
      fcitx::Key(FcitxKey_space, fcitx::KeyState::Shift),
      mainConfig.macroBypassModifier.value()));
  assert(!areca::isMacroBypassSpace(
      fcitx::Key(FcitxKey_space), mainConfig.macroBypassModifier.value()));
  assert(!areca::isMacroBypassSpace(
      fcitx::Key(FcitxKey_space,
                 fcitx::KeyStates{fcitx::KeyState::Shift,
                                  fcitx::KeyState::Ctrl}),
      mainConfig.macroBypassModifier.value()));
}
