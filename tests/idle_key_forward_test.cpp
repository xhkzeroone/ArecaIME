#include <cassert>
#include <cstdint>
#include <functional>
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
    commitTimesUsec.push_back(fcitx::now(CLOCK_MONOTONIC));
    if (onCommit) onCommit();
  }
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &event) override {
    events.push_back(std::string(event.isRelease() ? "up:" : "down:") +
                     fcitx::Key::keySymToUTF8(event.rawKey().sym()));
  }
  void updatePreeditImpl() override {}

  std::vector<std::string> events;
  std::vector<uint64_t> commitTimesUsec;
  std::function<void()> onCommit;
};

class TestEngine final : public areca::VietnameseEngine {
public:
  bool canProcessKey(uint32_t) const override { return true; }

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

  // Phím đầu không bị nhận/chèn lặp, nhưng Bamboo phải nhớ nó để ghép dấu.
  fcitx::KeyEvent first(&inputContext, fcitx::Key(FcitxKey_a));
  assert(scheduler.handleIdleKey(first, 'a', "a"));
  assert(!first.accepted());
  assert(inputContext.events.empty());
  assert(engine.currentText() == "a");

  // Đã có từ đang ghép: từ chối đường tắt mà không sửa trạng thái engine.
  fcitx::KeyEvent next(&inputContext, fcitx::Key(FcitxKey_s));
  assert(!scheduler.handleIdleKey(next, 's', "s"));
  assert(engine.currentText() == "a");

  engine.reset();
  // Engine đổi đầu ra thì phải chặn phím gốc và commit đúng một lần.
  fcitx::KeyEvent transformed(&inputContext, fcitx::Key(FcitxKey_x));
  assert(scheduler.handleIdleKey(transformed, 'x', "x"));
  assert(transformed.accepted());
  assert(inputContext.events == std::vector<std::string>{"commit:X"});

  inputContext.events.clear();
  // Dù engine rỗng, rewrite/queue đang chờ vẫn phải giữ thứ tự giao phím.
  scheduler.enqueue(inputContext, 'r', "r");
  assert(scheduler.rewritePending());
  engine.reset();
  fcitx::KeyEvent pending(&inputContext, fcitx::Key(FcitxKey_a));
  assert(!scheduler.handleIdleKey(pending, 'a', "a"));
  assert(engine.currentText().empty());
  scheduler.enqueue(inputContext, 'a', "a");
  assert(!scheduler.handleIdleKey(pending, 'a', "a"));
  backend.complete();
  assert(inputContext.events == std::vector<std::string>{"commit:a"});

  // Một phím được forward trực tiếp vẫn phải giữ single-flight barrier. Phím
  // kế tiếp đến sớm nằm trong queue và chỉ được commit sau đủ 20 ms.
  fcitx::EventLoop barrierEventLoop;
  TestInputContext barrierInputContext(manager);
  TestEngine barrierEngine;
  PendingBackend barrierBackend;
  areca::InputScheduler barrierScheduler(
      barrierEventLoop,
      [&](fcitx::InputContext &) -> areca::VietnameseEngine * {
        return &barrierEngine;
      },
      [] {
        areca::SchedulerTiming timing;
        timing.postCommitDelayMs = 20;
        timing.timerAccuracyUsec = 1;
        return timing;
      },
      [] { return false; },
      [&](fcitx::InputContext &, const areca::BambooResult &result) {
        return areca::RewriteBackendSelection{
            result.deleteCount ? &barrierBackend : nullptr};
      });

  const uint64_t forwardStartUsec = fcitx::now(CLOCK_MONOTONIC);
  fcitx::KeyEvent barrierFirst(&barrierInputContext,
                               fcitx::Key(FcitxKey_a));
  assert(barrierScheduler.handleIdleKey(barrierFirst, 'a', "a"));
  assert(!barrierFirst.accepted());

  fcitx::KeyEvent barrierSecond(&barrierInputContext,
                                fcitx::Key(FcitxKey_s));
  assert(!barrierScheduler.handleIdleKey(barrierSecond, 's', "s"));
  barrierScheduler.enqueue(barrierInputContext, 's', "s");
  assert(barrierScheduler.queuedKeyCount() == 1);
  assert(barrierInputContext.events.empty());

  barrierInputContext.onCommit = [&] { barrierEventLoop.exit(); };
  assert(barrierEventLoop.exec());
  assert(barrierInputContext.events ==
         std::vector<std::string>{"commit:s"});
  assert(barrierInputContext.commitTimesUsec.size() == 1);
  assert(barrierInputContext.commitTimesUsec.front() - forwardStartUsec >=
         20000);
}
