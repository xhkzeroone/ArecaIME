#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "rewrite_backend.h"
#include "surrounding_snapshot.h"
#include "surrounding_text_backend.h"
#include "types.h"

namespace {

class DummyInputContext final : public fcitx::InputContext {
public:
  DummyInputContext(fcitx::InputContextManager &manager,
                    std::vector<std::string> &events,
                    std::string program = "test-app")
      : fcitx::InputContext(manager, std::move(program)), events_(events) {}
  ~DummyInputContext() override { destroy(); }

  const char *frontend() const override { return "wayland"; }
  void commitStringImpl(const std::string &text) override {
    events_.push_back("commit:" + text);
    committedText += text;
  }
  void deleteSurroundingTextImpl(int offset, unsigned int count) override {
    events_.push_back("deleteSurrounding:" + std::to_string(offset) + ":" +
                      std::to_string(count));
    if (onDelete) {
      onDelete();
    }
  }
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}

  std::string committedText;
  std::function<void()> onDelete;

private:
  std::vector<std::string> &events_;
};

void testSnapshotEndsWith() {
  // Snapshot trễ: ô có "go" nhưng frontend báo "g".
  assert(!areca::surroundingSnapshotEndsWith(true, "g", 1, 1, "go"));
  assert(areca::surroundingSnapshotEndsWith(true, "go", 2, 2, "go"));
  assert(areca::surroundingSnapshotEndsWith(true, "xin go", 6, 6, "go"));
  assert(areca::surroundingSnapshotEndsWith(true, "go tiep", 2, 2, "go"));
  assert(!areca::surroundingSnapshotEndsWith(true, "go tiep", 7, 7, "go"));
  assert(areca::surroundingSnapshotEndsWith(true, "gõ", 2, 2, "gõ"));
  assert(!areca::surroundingSnapshotEndsWith(false, "", 0, 0, "go"));
  assert(!areca::surroundingSnapshotEndsWith(true, "go", 2, 0, "go"));
  assert(areca::surroundingSnapshotEndsWith(true, "", 0, 0, ""));
  assert(areca::surroundingSnapshotEndsWith(true, "gõ\u00a0", 3, 3, "gõ "));
  // Snapshot chưa có dấu cách Areca vừa commit (log anvildroid-gui).
  assert(!areca::surroundingSnapshotEndsWith(true, "gõ", 2, 2, "gõ "));

  // Text đã biết sau mỗi lần commit: xoá ký tự cuối rồi chèn phần mới.
  std::string known;
  areca::updateKnownTextBeforeCursor(known, 0, "go");
  areca::updateKnownTextBeforeCursor(known, 1, "õ");
  areca::updateKnownTextBeforeCursor(known, 0, " ");
  assert(known == "gõ ");
  areca::updateKnownTextBeforeCursor(known, 0, "ngo");
  areca::updateKnownTextBeforeCursor(known, 3, "ngô");
  assert(known == "gõ ngô");
  // Xoá nhiều hơn phần đã biết: chỉ còn chắc phần vừa chèn.
  areca::updateKnownTextBeforeCursor(known, 10, "tiếng");
  assert(known == "tiếng");
  // Giới hạn kích thước nhưng giữ phần đuôi và biên ký tự UTF-8.
  for (int i = 0; i < 100; ++i) {
    areca::updateKnownTextBeforeCursor(known, 0, "ỗ");
  }
  assert(known.size() <= areca::kMaxKnownTextBytes);
  assert(known.ends_with("ỗỗ"));
  assert(fcitx::utf8::validate(known));
}

areca::RewritePlan makeCheckedPlan(uint64_t transactionId,
                                   uint32_t backspaceCount,
                                   std::string expected, std::string commitText,
                                   uint32_t checkWaitMs) {
  areca::RewritePlan plan;
  plan.transactionId = transactionId;
  plan.backspaceCount = backspaceCount;
  plan.commitText = std::move(commitText);
  plan.expectedTextBeforeCursor = std::move(expected);
  plan.surroundingCheckWaitMs = checkWaitMs;
  plan.surroundingPollMs = 1;
  return plan;
}

std::unique_ptr<fcitx::EventSourceTime>
addTimer(fcitx::EventLoop &eventLoop, uint32_t delayMs,
         std::function<void()> callback) {
  auto timer = eventLoop.addTimeEvent(
      CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + delayMs * 1000ULL, 1,
      [callback = std::move(callback)](fcitx::EventSourceTime *, uint64_t) {
        callback();
        return false;
      });
  timer->setOneShot();
  return timer;
}

// Snapshot đã có đúng từ đang gõ: xoá rồi commit liền, không tự sửa snapshot.
void testCheckedSnapshotDeletesThenCommits() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  inputContext.surroundingText().setText("xin go", 6, 6);

  areca::SurroundingTextBackend backend(eventLoop, []() { return false; });
  bool done = false;
  const auto status =
      backend.apply(inputContext, makeCheckedPlan(301, 1, "go", "õ", 200),
                    [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                      assert(transactionId == 301);
                      assert(outcome == areca::RewriteOutcome::Succeeded);
                      done = true;
                      eventLoop.exit();
                    });
  assert(status == areca::ApplyStatus::Pending);
  assert(events.empty());
  eventLoop.exec();

  assert(done);
  assert(!backend.hasPending());
  assert((events ==
          std::vector<std::string>{"deleteSurrounding:-1:1", "commit:õ"}));
  assert(inputContext.surroundingText().text() == "xin go");
}

// Snapshot trễ một nhịp: chỉ xoá sau khi ứng dụng báo đủ từ đang gõ.
void testStaleSnapshotWaitsForApp() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  inputContext.surroundingText().setText("g", 1, 1);

  areca::SurroundingTextBackend backend(eventLoop, []() { return false; });
  auto update = addTimer(eventLoop, 10, [&]() {
    events.push_back("update");
    inputContext.surroundingText().setText("go", 2, 2);
  });
  const uint64_t startUsec = fcitx::now(CLOCK_MONOTONIC);
  uint64_t doneUsec = 0;
  backend.apply(inputContext, makeCheckedPlan(302, 1, "go", "õ", 500),
                [&](uint64_t, areca::RewriteOutcome) {
                  doneUsec = fcitx::now(CLOCK_MONOTONIC);
                  eventLoop.exit();
                });
  eventLoop.exec();

  assert((events == std::vector<std::string>{"update", "deleteSurrounding:-1:1",
                                             "commit:õ"}));
  assert(doneUsec - startUsec >= 10 * 1000);
  // Khớp là đi tiếp, không chờ tới hết giờ.
  assert(doneUsec - startUsec < 400 * 1000);
}

// Ứng dụng không báo đúng trong thời gian chờ: chưa xoá hay commit gì, trả
// NeedsFallback để scheduler chạy lại lượt đó bằng backend khác.
void testCheckTimeoutNeedsFallback() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  inputContext.surroundingText().setText("g", 1, 1);

  areca::SurroundingTextBackend backend(eventLoop, []() { return false; });
  const uint64_t startUsec = fcitx::now(CLOCK_MONOTONIC);
  uint64_t doneUsec = 0;
  backend.apply(inputContext, makeCheckedPlan(303, 1, "go", "õ", 20),
                [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                  assert(transactionId == 303);
                  assert(outcome == areca::RewriteOutcome::NeedsFallback);
                  doneUsec = fcitx::now(CLOCK_MONOTONIC);
                  eventLoop.exit();
                });
  eventLoop.exec();

  assert(events.empty());
  assert(doneUsec - startUsec >= 20 * 1000);
  assert(!backend.hasPending());
}

// Mất ô nhập lúc đang chờ: báo xong, không xoá hay commit.
void testContextLostDuringCheck() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  auto inputContext = std::make_unique<DummyInputContext>(manager, events);
  inputContext->surroundingText().setText("g", 1, 1);

  areca::SurroundingTextBackend backend(eventLoop, []() { return false; });
  auto drop = addTimer(eventLoop, 5, [&]() { inputContext.reset(); });
  bool done = false;
  backend.apply(*inputContext, makeCheckedPlan(304, 1, "go", "õ", 500),
                [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                  assert(transactionId == 304);
                  assert(outcome == areca::RewriteOutcome::Succeeded);
                  done = true;
                  eventLoop.exit();
                });
  eventLoop.exec();

  assert(done);
  assert(events.empty());
}

// Lượt đầu của ô: xoá xong chờ snapshot thấy chữ đã mất rồi mới commit.
void testVerifiedDeleteCommitsAfterSnapshot() {
  // Matcher: snapshot cũ, trạng thái trung gian và snapshot trễ không tính.
  assert(!areca::surroundingDeleteMatches(true, "g", 1, "", 0, "go", 2, 2));
  assert(!areca::surroundingDeleteMatches(true, "g", 1, "", 0, "o", 1, 1));
  assert(areca::surroundingDeleteMatches(true, "g", 1, "", 0, "", 0, 0));
  assert(
      areca::surroundingDeleteMatches(true, "a\n\n", 1, "\n\n", 0, "\n", 0, 0));
  assert(!areca::surroundingDeleteMatches(true, "xin go", 6, "xin g", 5,
                                          "xin go", 6, 6));
  assert(!areca::surroundingDeleteMatches(true, "xin go", 6, "xin g", 5,
                                          "xin go", 5, 5));
  assert(areca::surroundingDeleteMatches(true, "xin go", 6, "xin g", 5, "xin g",
                                         5, 5));

  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  inputContext.surroundingText().setText("xin go", 6, 6);
  std::unique_ptr<fcitx::EventSourceTime> update;
  inputContext.onDelete = [&]() {
    update = addTimer(eventLoop, 10, [&]() {
      events.push_back("update");
      inputContext.surroundingText().setText("xin g", 5, 5);
    });
  };

  std::vector<bool> checks;
  areca::SurroundingTextBackend backend(
      eventLoop, []() { return false; },
      [&](fcitx::InputContext &, bool confirmed) {
        checks.push_back(confirmed);
      });
  auto plan = makeCheckedPlan(305, 1, "go", "õ", 200);
  plan.verifySurroundingDelete = true;
  plan.surroundingDeleteCheckWaitMs = 500;
  bool done = false;
  backend.apply(inputContext, plan,
                [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                  assert(transactionId == 305);
                  assert(outcome == areca::RewriteOutcome::Succeeded);
                  done = true;
                  eventLoop.exit();
                });
  eventLoop.exec();

  assert(done);
  assert((checks == std::vector<bool>{true}));
  assert((events == std::vector<std::string>{"deleteSurrounding:-1:1", "update",
                                             "commit:õ"}));
}

// Ứng dụng báo surrounding đúng nhưng lờ lệnh xoá: không commit, báo verdict
// hỏng và trả NeedsFallback để chạy lại lượt đó bằng Backspace.
void testIgnoredDeleteNeedsFallback() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  inputContext.surroundingText().setText("xin go", 6, 6);

  std::vector<bool> checks;
  areca::SurroundingTextBackend backend(
      eventLoop, []() { return false; },
      [&](fcitx::InputContext &, bool confirmed) {
        checks.push_back(confirmed);
      });
  auto plan = makeCheckedPlan(306, 1, "go", "õ", 200);
  plan.verifySurroundingDelete = true;
  plan.surroundingDeleteCheckWaitMs = 20;
  const uint64_t startUsec = fcitx::now(CLOCK_MONOTONIC);
  uint64_t doneUsec = 0;
  backend.apply(inputContext, plan,
                [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                  assert(transactionId == 306);
                  assert(outcome == areca::RewriteOutcome::NeedsFallback);
                  doneUsec = fcitx::now(CLOCK_MONOTONIC);
                  eventLoop.exit();
                });
  eventLoop.exec();

  assert((checks == std::vector<bool>{false}));
  assert((events == std::vector<std::string>{"deleteSurrounding:-1:1"}));
  assert(inputContext.committedText.empty());
  assert(doneUsec - startUsec >= 20 * 1000);
  assert(!backend.hasPending());
}

} // namespace

int main() {
  testSnapshotEndsWith();
  testCheckedSnapshotDeletesThenCommits();
  testStaleSnapshotWaitsForApp();
  testCheckTimeoutNeedsFallback();
  testContextLostDuringCheck();
  testVerifiedDeleteCommitsAfterSnapshot();
  testIgnoredDeleteNeedsFallback();
  std::cout << "All surrounding text backend tests passed\n";
  return 0;
}
