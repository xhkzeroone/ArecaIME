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
#include "native_backspace_backend.h"
#include "native_keyboard_device.h"

namespace {

class DummyInputContext final : public fcitx::InputContext {
public:
  DummyInputContext(fcitx::InputContextManager &manager,
                    std::vector<std::string> &events)
      : fcitx::InputContext(manager, "xterm"), events_(events) {}
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

class FakeNativeDevice final : public areca::NativeKeyboardDevice {
public:
  explicit FakeNativeDevice(std::vector<std::string> &events)
      : events_(events) {}

  bool isAvailable() override { return available; }

  bool sendBackspace() override {
    const bool succeeds = sendResults.empty() || sendResults.front();
    if (!sendResults.empty()) {
      sendResults.erase(sendResults.begin());
    }
    events_.push_back(succeeds ? "backspace" : "backspace-failed");
    ++sendCalls;
    return succeeds;
  }

  bool available = true;
  uint32_t sendCalls = 0;
  std::vector<bool> sendResults;

private:
  std::vector<std::string> &events_;
};

areca::RewritePlan makePlan(uint64_t transactionId, uint32_t backspaceCount,
                            std::string commitText) {
  areca::RewritePlan plan;
  plan.transactionId = transactionId;
  plan.backspaceCount = backspaceCount;
  plan.xtestBackspaceDelayMs = 0;
  plan.afterXTestBackspaceWaitMs = 0;
  plan.waylandAfterXTestBackspaceWaitMs = 0;
  plan.ximAfterXTestBackspaceWaitMs = 0;
  plan.fcitx4AfterXTestBackspaceWaitMs = 0;
  plan.dbusAfterXTestBackspaceWaitMs = 0;
  plan.commitText = std::move(commitText);
  return plan;
}

void testSuccessfulRewrite() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  FakeNativeDevice device(events);
  areca::AdaptiveWait adaptiveWait;
  areca::NativeBackspaceBackend backend(eventLoop, device, adaptiveWait,
                                        [] { return false; });

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
  assert(device.sendCalls == 3);
  assert(inputContext.committedText == "test");
  assert(
      (events == std::vector<std::string>{"backspace", "backspace", "backspace",
                                          "commit:test", "done"}));
}

void testUnavailableDevice() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  FakeNativeDevice device(events);
  device.available = false;
  areca::AdaptiveWait adaptiveWait;
  areca::NativeBackspaceBackend backend(eventLoop, device, adaptiveWait,
                                        [] { return false; });

  bool doneCalled = false;
  const auto status = backend.apply(
      inputContext, makePlan(100, 1, "ignored"),
      [&](uint64_t, areca::RewriteOutcome) { doneCalled = true; });

  assert(status == areca::ApplyStatus::Failed);
  assert(!backend.hasPending());
  assert(!doneCalled);
  assert(device.sendCalls == 0);
  assert(inputContext.committedText.empty());
}

void testInitialSendFailure() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  FakeNativeDevice device(events);
  device.sendResults = {false};
  areca::AdaptiveWait adaptiveWait;
  areca::NativeBackspaceBackend backend(eventLoop, device, adaptiveWait,
                                        [] { return false; });

  bool doneCalled = false;
  const auto status = backend.apply(
      inputContext, makePlan(101, 1, "must-not-commit"),
      [&](uint64_t, areca::RewriteOutcome) { doneCalled = true; });

  assert(status == areca::ApplyStatus::Failed);
  assert(!backend.hasPending());
  assert(!doneCalled);
  assert(device.sendCalls == 1);
  assert(inputContext.committedText.empty());
  assert((events == std::vector<std::string>{"backspace-failed"}));
}

void testAsynchronousSendFailure() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  FakeNativeDevice device(events);
  device.sendResults = {true, false};
  areca::AdaptiveWait adaptiveWait;
  areca::NativeBackspaceBackend backend(eventLoop, device, adaptiveWait,
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
  assert(device.sendCalls == 2);
  assert(inputContext.committedText.empty());
  assert((events ==
          std::vector<std::string>{"backspace", "backspace-failed", "failed"}));
}

} // namespace

int main() {
  testSuccessfulRewrite();
  testUnavailableDevice();
  testInitialSendFailure();
  testAsynchronousSendFailure();
  std::cout << "NativeBackspaceBackend test passed successfully\n";
  return 0;
}
