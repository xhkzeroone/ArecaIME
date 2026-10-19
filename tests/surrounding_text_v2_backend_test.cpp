#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "surrounding_text_v2_backend.h"
#include "types.h"

namespace {

class DummyInputContext final : public fcitx::InputContext {
public:
  DummyInputContext(fcitx::InputContextManager &manager,
                    std::vector<std::string> &events)
      : fcitx::InputContext(manager, "test-app"), events_(events) {}
  ~DummyInputContext() override { destroy(); }

  const char *frontend() const override { return "wayland"; }
  void commitStringImpl(const std::string &text) override {
    events_.push_back("commit:" + text);
  }
  void deleteSurroundingTextImpl(int offset, unsigned int count) override {
    events_.push_back("delete:" + std::to_string(offset) + ":" +
                      std::to_string(count));
    if (onDelete) {
      onDelete();
    }
  }
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}

  std::function<void()> onDelete;

private:
  std::vector<std::string> &events_;
};

class FakeFallbackBackend final : public areca::RewriteBackend {
public:
  explicit FakeFallbackBackend(bool &called) : called_(called) {}

  const char *name() const override { return "fake-fallback"; }
  areca::ApplyStatus apply(fcitx::InputContext &inputContext,
                           const areca::RewritePlan &plan,
                           areca::RewriteDone onDone) override {
    called_ = true;
    backspaceCount_ = plan.backspaceCount;
    inputContext.commitString(plan.commitText);
    if (onDone) {
      onDone(plan.transactionId, areca::RewriteOutcome::Succeeded);
    }
    return areca::ApplyStatus::Completed;
  }

  uint32_t backspaceCount() const { return backspaceCount_; }

private:
  bool &called_;
  uint32_t backspaceCount_ = 0;
};

areca::RewritePlan makeProbePlan(uint64_t transactionId,
                                 uint32_t backspaceCount = 1,
                                 bool probeSurrounding = true) {
  areca::RewritePlan plan;
  plan.transactionId = transactionId;
  plan.backspaceCount = backspaceCount;
  plan.surroundingDeleteDelayMs = 0;
  plan.waylandSurroundingDeleteDelayMs = 0;
  plan.afterSurroundingDeleteWaitMs = 1;
  plan.commitText = "õ";
  plan.probeSurrounding = probeSurrounding;
  return plan;
}

void testProbeSuccess() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  inputContext.surroundingText().setText("go", 2, 2);

  bool probeSucceeded = false;
  bool fallbackCalled = false;
  bool doneCalled = false;
  FakeFallbackBackend fallback(fallbackCalled);
  areca::SurroundingTextV2Backend backend(
      eventLoop, []() { return false; },
      [&](fcitx::InputContext &) { probeSucceeded = true; }, nullptr,
      [&](fcitx::InputContext &) -> areca::RewriteBackend * {
        return &fallback;
      });
  inputContext.onDelete = [&]() {
    inputContext.surroundingText().setText("g", 1, 1);
    backend.notifySurroundingTextUpdated(inputContext);
  };

  const auto status =
      backend.apply(inputContext, makeProbePlan(301, 1, false),
                    [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                      assert(transactionId == 301);
                      assert(outcome == areca::RewriteOutcome::Succeeded);
                      doneCalled = true;
                      eventLoop.exit();
                    });
  assert(status == areca::ApplyStatus::Pending);
  eventLoop.exec();

  assert(probeSucceeded);
  assert(!fallbackCalled);
  assert(doneCalled);
  assert(events == std::vector<std::string>({"delete:-1:1", "commit:õ"}));
}

void testCommitWaitsForFinalDeleteUpdate() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  inputContext.surroundingText().setText("go", 2, 2);

  bool doneCalled = false;
  areca::SurroundingTextV2Backend backend(eventLoop, []() { return false; });
  const auto status =
      backend.apply(inputContext, makeProbePlan(305, 1, false),
                    [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                      assert(transactionId == 305);
                      assert(outcome == areca::RewriteOutcome::Succeeded);
                      doneCalled = true;
                      eventLoop.exit();
                    });

  assert(status == areca::ApplyStatus::Pending);
  assert(backend.isProbing());
  assert(!doneCalled);
  assert(events == std::vector<std::string>({"delete:-1:1"}));

  inputContext.surroundingText().setText("g", 1, 1);
  backend.notifySurroundingTextUpdated(inputContext);
  eventLoop.exec();

  assert(doneCalled);
  assert(events == std::vector<std::string>({"delete:-1:1", "commit:õ"}));
}

void testProbeFailureFallsBack() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  inputContext.surroundingText().setText("go", 2, 2);

  bool probeFailed = false;
  bool fallbackCalled = false;
  bool doneCalled = false;
  FakeFallbackBackend fallback(fallbackCalled);
  areca::SurroundingTextV2Backend backend(
      eventLoop, []() { return false; }, nullptr,
      [&](fcitx::InputContext &) { probeFailed = true; },
      [&](fcitx::InputContext &) -> areca::RewriteBackend * {
        return &fallback;
      });

  const auto status =
      backend.apply(inputContext, makeProbePlan(302),
                    [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                      assert(transactionId == 302);
                      assert(outcome == areca::RewriteOutcome::Succeeded);
                      doneCalled = true;
                      eventLoop.exit();
                    });
  assert(status == areca::ApplyStatus::Pending);
  eventLoop.exec();

  assert(probeFailed);
  assert(fallbackCalled);
  assert(fallback.backspaceCount() == 1);
  assert(doneCalled);
  assert(events == std::vector<std::string>({"delete:-1:1", "commit:õ"}));
}

void testSingleWrongUpdateFallsBack() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  inputContext.surroundingText().setText("go", 2, 2);

  bool probeSucceeded = false;
  bool probeFailed = false;
  bool fallbackCalled = false;
  bool doneCalled = false;
  FakeFallbackBackend fallback(fallbackCalled);
  areca::SurroundingTextV2Backend backend(
      eventLoop, []() { return false; },
      [&](fcitx::InputContext &) { probeSucceeded = true; },
      [&](fcitx::InputContext &) { probeFailed = true; },
      [&](fcitx::InputContext &) -> areca::RewriteBackend * {
        return &fallback;
      });

  auto plan = makeProbePlan(306);
  plan.surroundingProbeTimeoutMs = 5;
  const auto status = backend.apply(
      inputContext, plan,
      [&](uint64_t, areca::RewriteOutcome outcome) {
        assert(outcome == areca::RewriteOutcome::Succeeded);
        doneCalled = true;
        eventLoop.exit();
      });
  assert(status == areca::ApplyStatus::Pending);

  backend.notifySurroundingTextUpdated(inputContext);
  inputContext.surroundingText().setText("g", 1, 1);
  eventLoop.exec();

  assert(!probeSucceeded);
  assert(probeFailed);
  assert(fallbackCalled);
  assert(doneCalled);
}

void testProbeChecksFinalDelete() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  inputContext.surroundingText().setText("goo", 3, 3);

  bool probeSucceeded = false;
  bool doneCalled = false;
  unsigned int deletes = 0;
  areca::SurroundingTextV2Backend backend(
      eventLoop, []() { return false; },
      [&](fcitx::InputContext &) { probeSucceeded = true; });
  inputContext.onDelete = [&]() {
    ++deletes;
    if (deletes == 1) {
      assert(backend.isProbing());
      assert(!probeSucceeded);
      return;
    }
    inputContext.surroundingText().setText("g", 1, 1);
    backend.notifySurroundingTextUpdated(inputContext);
  };

  const auto status =
      backend.apply(inputContext, makeProbePlan(303, 2),
                    [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                      assert(transactionId == 303);
                      assert(outcome == areca::RewriteOutcome::Succeeded);
                      doneCalled = true;
                      eventLoop.exit();
                    });
  assert(status == areca::ApplyStatus::Pending);
  eventLoop.exec();

  assert(probeSucceeded);
  assert(doneCalled);
  assert(events ==
         std::vector<std::string>({"delete:-1:1", "delete:-1:1", "commit:õ"}));
}

void testPartialProbeFallsBackOnlyRemainingDelete() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  inputContext.surroundingText().setText("goo", 3, 3);

  areca::AdaptiveWait adaptiveWait;
  adaptiveWait.observeSystemStress(20);
  bool probeFailed = false;
  bool fallbackCalled = false;
  bool doneCalled = false;
  FakeFallbackBackend fallback(fallbackCalled);
  areca::SurroundingTextV2Backend backend(
      eventLoop, []() { return false; }, nullptr,
      [&](fcitx::InputContext &) { probeFailed = true; },
      [&](fcitx::InputContext &) -> areca::RewriteBackend * {
        return &fallback;
      },
      &adaptiveWait);

  const auto status =
      backend.apply(inputContext, makeProbePlan(304, 2),
                    [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                      assert(transactionId == 304);
                      assert(outcome == areca::RewriteOutcome::Succeeded);
                      doneCalled = true;
                      eventLoop.exit();
                    });
  assert(status == areca::ApplyStatus::Pending);
  eventLoop.exec();

  assert(probeFailed);
  assert(fallbackCalled);
  assert(fallback.backspaceCount() == 1);
  assert(doneCalled);
  assert(events ==
         std::vector<std::string>({"delete:-1:1", "delete:-1:1", "commit:õ"}));
}

} // namespace

int main() {
  fcitx::EventLoop eventLoop;
  areca::SurroundingTextV2Backend backend(eventLoop, []() { return false; });

  assert(std::string(backend.name()) == "surrounding-text-v2");
  assert(!backend.hasPending());

  areca::RewritePlan plan;
  assert(plan.surroundingDeleteDelayMs == 10);
  assert(plan.waylandSurroundingDeleteDelayMs == 0);
  assert(plan.afterSurroundingDeleteWaitMs == 1);

  areca::AdaptiveWait adaptiveWait;
  areca::SurroundingTextV2Backend backendWithAdaptive(
      eventLoop, []() { return false; }, nullptr, nullptr, nullptr,
      &adaptiveWait);
  assert(std::string(backendWithAdaptive.name()) == "surrounding-text-v2");

  testProbeSuccess();
  testCommitWaitsForFinalDeleteUpdate();
  testProbeFailureFallsBack();
  testSingleWrongUpdateFallsBack();
  testProbeChecksFinalDelete();
  testPartialProbeFallsBackOnlyRemainingDelete();

  std::cout << "SurroundingTextV2Backend test passed\n";
  return 0;
}
