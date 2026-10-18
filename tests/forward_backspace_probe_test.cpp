#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "forward_backspace_backend.h"

namespace {

class TestInputContext final : public fcitx::InputContext {
public:
  TestInputContext(fcitx::InputContextManager &manager,
                   std::vector<std::string> &events)
      : fcitx::InputContext(manager, "probe-test"), events_(events) {}
  ~TestInputContext() override { destroy(); }

  const char *frontend() const override { return "dbus"; }

  void commitStringImpl(const std::string &text) override {
    events_.push_back("commit:" + text);
  }
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &event) override {
    events_.push_back(event.isRelease() ? "backspace-up" : "backspace-down");
  }
  void updatePreeditImpl() override {}

private:
  std::vector<std::string> &events_;
};

} // namespace

int main() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  TestInputContext inputContext(manager, events);
  inputContext.surroundingText().setText("go", 2, 2);

  areca::AdaptiveWait adaptiveWait;
  areca::ForwardBackspaceBackend backend(eventLoop, adaptiveWait,
                                         []() { return false; });
  areca::RewritePlan plan;
  plan.transactionId = 1;
  plan.backspaceCount = 1;
  plan.afterBackspaceWaitMs = 1;
  plan.commitText = "õ";

  bool done = false;
  const auto status =
      backend.apply(inputContext, plan,
                    [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                      assert(transactionId == 1);
                      assert(outcome == areca::RewriteOutcome::Succeeded);
                      done = true;
                      eventLoop.exit();
                    });

  assert(status == areca::ApplyStatus::Pending);
  assert(!done);
  assert(events ==
         std::vector<std::string>({"backspace-down", "backspace-up"}));

  eventLoop.exec();

  assert(done);
  assert(events == std::vector<std::string>(
                       {"backspace-down", "backspace-up", "commit:õ"}));
  std::cout << "ForwardBackspaceBackend test passed\n";
  return 0;
}
