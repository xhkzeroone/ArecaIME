#include <cassert>
#include <string>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "input_scheduler.h"

namespace {

class TestInputContext final : public fcitx::InputContext {
public:
  explicit TestInputContext(fcitx::InputContextManager &manager)
      : fcitx::InputContext(manager, "google-chrome") {}
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
  const std::string &currentText() const override { return current_; }

private:
  std::string current_;
};

class PendingBackend final : public areca::RewriteBackend {
public:
  const char *name() const override { return "pending-test"; }

  areca::ApplyStatus apply(fcitx::InputContext &,
                           const areca::RewritePlan &plan,
                           areca::RewriteDone onDone) override {
    transactionId_ = plan.transactionId;
    onDone_ = std::move(onDone);
    return areca::ApplyStatus::Pending;
  }

  void complete() {
    assert(onDone_);
    auto onDone = std::move(onDone_);
    onDone(transactionId_);
  }

private:
  uint64_t transactionId_ = 0;
  areca::RewriteDone onDone_;
};

} // namespace

int main() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  TestInputContext inputContext(manager);
  TestEngine engine;
  PendingBackend backend;

  areca::InputScheduler scheduler(
      eventLoop,
      [&](fcitx::InputContext &) -> areca::VietnameseEngine * {
        return &engine;
      },
      [] {
        areca::SchedulerTiming timing;
        timing.postCommitDelayMs = 0;
        return timing;
      },
      [] { return false; },
      [&](fcitx::InputContext &, const areca::BambooResult &result) {
        return areca::RewriteBackendSelection{
            result.deleteCount ? &backend : nullptr};
      });

  scheduler.enqueue(inputContext, 'a', "a");
  // Phím đã enqueue được commit, không phát lại cặp press/release giả.
  const std::vector<std::string> committed = {"commit:a"};
  assert(inputContext.events == committed);

  inputContext.events.clear();
  scheduler.enqueue(inputContext, 'x', "x");
  const std::vector<std::string> transformed = {"commit:X"};
  assert(inputContext.events == transformed);

  inputContext.events.clear();
  scheduler.enqueue(inputContext, 'A', "A");
  const std::vector<std::string> syntheticText = {"commit:A"};
  assert(inputContext.events == syntheticText);

  inputContext.events.clear();
  scheduler.enqueue(inputContext, 'r', "r");
  scheduler.enqueue(inputContext, 'a', "a");
  scheduler.enqueue(inputContext, 'b', "b");
  // Backend chưa hoàn tất: giữ FIFO và không xử lý trước các phím đang chờ.
  assert(scheduler.rewritePending());
  assert(scheduler.queuedKeyCount() == 2);
  const auto textBeforeCompletion = engine.currentText();
  assert(inputContext.events.empty());
  assert(textBeforeCompletion == "axAr");
  backend.complete();
  const std::vector<std::string> queuedCommits = {"commit:a", "commit:b"};
  assert(inputContext.events == queuedCommits);
  assert(engine.currentText() == "axArab");
  assert(!scheduler.rewritePending());
  assert(scheduler.queuedKeyCount() == 0);

  return 0;
}
