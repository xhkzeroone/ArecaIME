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
                            const char *program = "google-chrome")
      : fcitx::InputContext(manager, program) {}
  ~TestInputContext() override { destroy(); }

  const char *frontend() const override { return "wayland"; }

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

  areca::BambooResult processBackspace() override {
    ++backspaceProcessCount;
    macroUndoAvailable = false;
    spellcheckRestored = false;
    return {};
  }
  void backspace() override {
    ++plainBackspaceCount;
    spellcheckRestored = false;
  }
  void reset() override {
    current_.clear();
    spellcheckRestored = false;
  }
  bool restoreFromRenderedText(const std::string &text) override {
    if (text.empty()) {
      return false;
    }
    current_ = text;
    return true;
  }
  const std::string &currentText() const override { return current_; }
  bool canUndoMacro() const override { return macroUndoAvailable; }
  bool wasRestoredBySpellcheck() const override { return spellcheckRestored; }

  int backspaceProcessCount = 0;
  int plainBackspaceCount = 0;
  bool macroUndoAvailable = false;
  bool spellcheckRestored = false;

private:
  std::string current_;
};

} // namespace

int main() {
  // Macro undo phải chặn cứng auto-repeat và chỉ xử lý đúng một lần khi thả
  // Backspace, giống BackspaceRecovery thường.
  {
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
    TestInputContext context(manager, "org.gnome.TextEditor");
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
        [](fcitx::InputContext &) { return false; }, [] { return false; });

    auto *state = context.propertyFor(&factory);
    auto *engine = static_cast<TestEngine *>(state->engine.get());

    fcitx::KeyEvent press(&context, fcitx::Key(FcitxKey_BackSpace));
    handler.handleKeyEvent(press);
    assert(press.accepted());
    assert(state->backspaceRecoveryAwaitingRelease);
    assert(engine->backspaceProcessCount == 0);

    fcitx::KeyEvent repeat(
        &context,
        fcitx::Key(FcitxKey_BackSpace, fcitx::KeyState::Repeat));
    handler.handleKeyEvent(repeat);
    assert(repeat.accepted());
    assert(state->backspaceRecoveryAwaitingRelease);
    assert(engine->backspaceProcessCount == 0);

    fcitx::KeyEvent release(&context, fcitx::Key(FcitxKey_BackSpace), true);
    handler.handleKeyEvent(release);
    assert(release.accepted());
    assert(!state->backspaceRecoveryAwaitingRelease);
    assert(engine->backspaceProcessCount == 1);

    // Đợi qua cửa sổ bảo vệ phím inject sau transaction trước đó.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // BackspaceRecovery không được chặn repeat cho composition bình thường.
    engine->process('a', "a");
    fcitx::KeyEvent plainPress(&context, fcitx::Key(FcitxKey_BackSpace));
    handler.handleKeyEvent(plainPress);
    assert(!state->backspaceRecoveryAwaitingRelease);
    assert(engine->plainBackspaceCount == 1);

    // Chỉ từ vừa bị spellcheck restore mới chờ release và nuốt auto-repeat.
    engine->process('a', "a");
    engine->spellcheckRestored = true;
    fcitx::KeyEvent restoredPress(&context, fcitx::Key(FcitxKey_BackSpace));
    handler.handleKeyEvent(restoredPress);
    assert(restoredPress.accepted());
    assert(state->backspaceRecoveryAwaitingRelease);

    fcitx::KeyEvent restoredRepeat(
        &context,
        fcitx::Key(FcitxKey_BackSpace, fcitx::KeyState::Repeat));
    handler.handleKeyEvent(restoredRepeat);
    assert(restoredRepeat.accepted());
    assert(state->backspaceRecoveryAwaitingRelease);

    // X11/IBus có thể gửi release trung gian kèm Repeat. Release này vẫn phải
    // bị nuốt; chỉ release vật lý cuối cùng mới chạy recovery.
    fcitx::KeyEvent repeatedRelease(
        &context,
        fcitx::Key(FcitxKey_BackSpace, fcitx::KeyState::Repeat), true);
    handler.handleKeyEvent(repeatedRelease);
    assert(repeatedRelease.accepted());
    assert(state->backspaceRecoveryAwaitingRelease);
    assert(engine->backspaceProcessCount == 1);

    fcitx::KeyEvent repeatAfterRelease(
        &context,
        fcitx::Key(FcitxKey_BackSpace, fcitx::KeyState::Repeat));
    handler.handleKeyEvent(repeatAfterRelease);
    assert(repeatAfterRelease.accepted());
    assert(state->backspaceRecoveryAwaitingRelease);

    fcitx::KeyEvent restoredRelease(&context,
                                    fcitx::Key(FcitxKey_BackSpace), true);
    handler.handleKeyEvent(restoredRelease);
    assert(restoredRelease.accepted());
    assert(!state->backspaceRecoveryAwaitingRelease);
    assert(engine->backspaceProcessCount == 2);
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
        [] { return false; });
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
  restoreContext.surroundingText().setText("nh", 2, 2);
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
      [] { return true; });
  restoreHandler.activate(restoreContext);
  fcitx::KeyEvent restoreKey(&restoreContext, fcitx::Key(FcitxKey_a));
  restoreHandler.handleKeyEvent(restoreKey);
  assert(restoreKey.accepted());
  assert(restoreContext.events == std::vector<std::string>{"commit:a"});
  assert(restoreContext.propertyFor(&restoreFactory)->engine->currentText() ==
         "nha");

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
      [] { return true; });
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
}
