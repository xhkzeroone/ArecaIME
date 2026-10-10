#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "adaptive_wait.h"
#include "xtest_device.h"
#include "xtest_shift_select_backend.h"

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
    committedText += text;
  }
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}

  std::string committedText;

private:
  std::vector<std::string> &events_;
};

class FakeXTestDevice final : public areca::XTestBackspaceDevice {
public:
  explicit FakeXTestDevice(std::vector<std::string> &events)
      : events_(events) {}

  bool isAvailable() override { return available; }

  bool sendShift(bool press) override {
    const bool succeeds = shiftResults.empty() || shiftResults.front();
    if (!shiftResults.empty()) {
      shiftResults.erase(shiftResults.begin());
    }
    events_.push_back(press ? (succeeds ? "shift:down" : "shift:down-failed")
                            : (succeeds ? "shift:up" : "shift:up-failed"));
    return succeeds;
  }

  bool sendLeft() override {
    const bool succeeds = leftResults.empty() || leftResults.front();
    if (!leftResults.empty()) {
      leftResults.erase(leftResults.begin());
    }
    events_.push_back(succeeds ? "left" : "left-failed");
    ++leftCalls;
    return succeeds;
  }

  bool sendBackspace() override {
    events_.push_back("backspace");
    ++backspaceCalls;
    return true;
  }

  bool available = true;
  uint32_t leftCalls = 0;
  uint32_t backspaceCalls = 0;
  std::vector<bool> shiftResults;
  std::vector<bool> leftResults;

private:
  std::vector<std::string> &events_;
};

areca::RewritePlan makePlan(uint64_t transactionId, uint32_t backspaceCount,
                            std::string commitText) {
  areca::RewritePlan plan;
  plan.transactionId = transactionId;
  plan.backspaceCount = backspaceCount;
  plan.uinputShiftSelectDelayMs = 0;
  plan.waylandAfterUinputShiftSelectWaitMs = 0;
  plan.commitText = std::move(commitText);
  return plan;
}

void testSuccessfulShiftSelect() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  FakeXTestDevice device(events);
  areca::AdaptiveWait adaptiveWait;
  areca::XTestShiftSelectBackend backend(eventLoop, device, adaptiveWait,
                                        [] { return false; });

  assert(std::string(backend.name()) == "native-shift-select");
  assert(backend.isAvailable());

  bool doneCalled = false;
  const auto status =
      backend.apply(inputContext, makePlan(42, 3, "test"),
                    [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                      assert(transactionId == 42);
                      assert(outcome == areca::RewriteOutcome::Succeeded);
                      events.push_back("done");
                      doneCalled = true;
                      eventLoop.exit();
                    });

  assert(status == areca::ApplyStatus::Pending);
  assert(backend.hasPending());
  eventLoop.exec();

  assert(doneCalled);
  assert(!backend.hasPending());
  assert(device.leftCalls == 3);
  assert(inputContext.committedText == "test");
  assert((events == std::vector<std::string>{"shift:down", "left", "left",
                                             "left", "shift:up", "commit:t",
                                             "commit:est", "done"}));
}

void testEmptyCommitTextErasesWithBackspace() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  FakeXTestDevice device(events);
  areca::AdaptiveWait adaptiveWait;
  areca::XTestShiftSelectBackend backend(eventLoop, device, adaptiveWait,
                                        [] { return false; });

  bool doneCalled = false;
  const auto status =
      backend.apply(inputContext, makePlan(43, 2, ""),
                    [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                      assert(transactionId == 43);
                      assert(outcome == areca::RewriteOutcome::Succeeded);
                      events.push_back("done");
                      doneCalled = true;
                      eventLoop.exit();
                    });

  assert(status == areca::ApplyStatus::Pending);
  assert(backend.hasPending());
  eventLoop.exec();

  assert(doneCalled);
  assert(!backend.hasPending());
  assert(device.leftCalls == 2);
  assert(device.backspaceCalls == 1);
  assert(inputContext.committedText.empty());
  assert((events == std::vector<std::string>{"shift:down", "left", "left",
                                             "shift:up", "backspace", "done"}));
}

void testZeroBackspaceDirectCommit() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  FakeXTestDevice device(events);
  areca::AdaptiveWait adaptiveWait;
  areca::XTestShiftSelectBackend backend(eventLoop, device, adaptiveWait,
                                        [] { return false; });

  bool doneCalled = false;
  const auto status =
      backend.apply(inputContext, makePlan(44, 0, "hi"),
                    [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                      assert(transactionId == 44);
                      assert(outcome == areca::RewriteOutcome::Succeeded);
                      events.push_back("done");
                      doneCalled = true;
                      eventLoop.exit();
                    });

  assert(status == areca::ApplyStatus::Pending);
  assert(backend.hasPending());
  eventLoop.exec();

  assert(doneCalled);
  assert(!backend.hasPending());
  assert(device.leftCalls == 0);
  assert(inputContext.committedText == "hi");
  assert((events == std::vector<std::string>{"commit:h", "commit:i", "done"}));
}

void testUnavailableDevice() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  FakeXTestDevice device(events);
  device.available = false;
  areca::AdaptiveWait adaptiveWait;
  areca::XTestShiftSelectBackend backend(eventLoop, device, adaptiveWait,
                                        [] { return false; });

  assert(!backend.isAvailable());

  bool doneCalled = false;
  const auto status = backend.apply(
      inputContext, makePlan(100, 1, "ignored"),
      [&](uint64_t, areca::RewriteOutcome) { doneCalled = true; });

  assert(status == areca::ApplyStatus::Failed);
  assert(!backend.hasPending());
  assert(!doneCalled);
  assert(device.leftCalls == 0);
  assert(inputContext.committedText.empty());
}

void testInitialShiftFailure() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  FakeXTestDevice device(events);
  device.shiftResults = {false};
  areca::AdaptiveWait adaptiveWait;
  areca::XTestShiftSelectBackend backend(eventLoop, device, adaptiveWait,
                                        [] { return false; });

  bool doneCalled = false;
  const auto status = backend.apply(
      inputContext, makePlan(101, 1, "must-not-commit"),
      [&](uint64_t, areca::RewriteOutcome) { doneCalled = true; });

  assert(status == areca::ApplyStatus::Failed);
  assert(!backend.hasPending());
  assert(!doneCalled);
  assert(device.leftCalls == 0);
  assert(inputContext.committedText.empty());
  assert((events == std::vector<std::string>{"shift:down-failed"}));
}

void testAsynchronousLeftFailure() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  FakeXTestDevice device(events);
  device.leftResults = {true, false};
  areca::AdaptiveWait adaptiveWait;
  areca::XTestShiftSelectBackend backend(eventLoop, device, adaptiveWait,
                                        [] { return false; });

  bool doneCalled = false;
  const auto status =
      backend.apply(inputContext, makePlan(102, 2, "must-not-commit"),
                    [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                      assert(transactionId == 102);
                      assert(outcome == areca::RewriteOutcome::Failed);
                      events.push_back("failed");
                      doneCalled = true;
                      eventLoop.exit();
                    });

  assert(status == areca::ApplyStatus::Pending);
  eventLoop.exec();

  assert(doneCalled);
  assert(!backend.hasPending());
  assert(device.leftCalls == 2);
  assert(inputContext.committedText.empty());
  assert((events == std::vector<std::string>{"shift:down", "left",
                                             "left-failed", "shift:up",
                                             "failed"}));
}

} // namespace

int main() {
  testSuccessfulShiftSelect();
  testEmptyCommitTextErasesWithBackspace();
  testZeroBackspaceDirectCommit();
  testUnavailableDevice();
  testInitialShiftFailure();
  testAsynchronousLeftFailure();
  std::cout << "XTestShiftSelectBackend test passed successfully\n";
  return 0;
}
