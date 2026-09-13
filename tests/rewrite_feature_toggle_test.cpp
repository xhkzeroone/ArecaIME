#include <cassert>
#include <string>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

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

  areca::BambooResult processBackspace() override { return {}; }
  void backspace() override {}
  void reset() override { current_.clear(); }
  bool restoreFromRenderedText(const std::string &text) override {
    if (text.empty()) {
      return false;
    }
    current_ = text;
    return true;
  }
  const std::string &currentText() const override { return current_; }

private:
  std::string current_;
};

} // namespace

int main() {
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
        [] { areca::SchedulerTiming timing; timing.postCommitDelayMs = 0; return timing; },
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
        [] { areca::SchedulerTiming timing; timing.postCommitDelayMs = 0; return timing; },
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
      [] {
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
      [] {
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
  fcitx::KeyEvent trailingSpace(&boundaryContext, fcitx::Key(FcitxKey_space));
  boundaryHandler.handleKeyEvent(trailingSpace);
  assert(!boundaryContext.propertyFor(&boundaryFactory)
              ->surroundingRestoreArmed);
  fcitx::KeyEvent nextLetter(&boundaryContext, fcitx::Key(FcitxKey_a));
  boundaryHandler.handleKeyEvent(nextLetter);
  assert(boundaryContext.events ==
         std::vector<std::string>{"commit: ", "commit:a"});
  assert(boundaryContext.propertyFor(&boundaryFactory)->engine->currentText() ==
         " a");
}
