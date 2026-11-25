#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "input_scheduler.h"
#include "surrounding_text_cache.h"

namespace {

const std::string kMarker(areca::kRewriteMarker);

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
    events.push_back(std::string("forward:") + event.rawKey().toString() +
                     (event.isRelease() ? ":up" : ""));
  }
  void updatePreeditImpl() override {}

  std::vector<std::string> events;
};

// Mỗi phím "r" thay ký tự trước nó, như "e" biến "vie" thành "viê".
class TestEngine final : public areca::VietnameseEngine {
public:
  bool canProcessKey(uint32_t) const override { return true; }
  areca::BambooResult process(uint32_t, const std::string &text) override {
    areca::BambooResult result;
    result.deleteCount = text == "r" ? 1 : 0;
    result.commitText = text == "r" ? "ê" : text;
    return result;
  }
  areca::BambooResult processBackspace() override { return {}; }
  void backspace() override {}
  void reset() override {}
  const std::string &currentText() const override { return current_; }

private:
  std::string current_;
};

class RecordingBackend final : public areca::RewriteBackend {
public:
  explicit RecordingBackend(areca::MarkerDelivery delivery)
      : delivery_(delivery) {}

  const char *name() const override { return "recording"; }
  areca::MarkerDelivery markerDelivery() const override { return delivery_; }
  areca::ApplyStatus apply(fcitx::InputContext &inputContext,
                           const areca::RewritePlan &plan,
                           areca::RewriteDone) override {
    auto &context = static_cast<TestInputContext &>(inputContext);
    context.events.push_back("delete:" + std::to_string(plan.backspaceCount) +
                             (plan.typeMarker ? ":typed" : ""));
    appliedAtUsec = fcitx::now(CLOCK_MONOTONIC);
    inputContext.commitString(plan.commitText);
    return areca::ApplyStatus::Completed;
  }

  uint64_t appliedAtUsec = 0;

private:
  areca::MarkerDelivery delivery_;
};

struct Harness {
  explicit Harness(areca::MarkerDelivery delivery, uint32_t markerWaitMs = 10)
      : context(manager), backend(delivery),
        scheduler(
            loop, [this](fcitx::InputContext &) { return &engine; },
            [markerWaitMs](fcitx::InputContext &) {
              areca::SchedulerTiming timing;
              timing.postCommitDelayMs = 0;
              timing.waylandAfterMarkerCommitWaitMs = markerWaitMs;
              return timing;
            },
            [] { return false; },
            [this](fcitx::InputContext &, const areca::BambooResult &) {
              return areca::RewriteBackendSelection{&backend};
            }) {
    scheduler.setCommitObserver(
        [this](fcitx::InputContext &, uint32_t count, const std::string &text) {
          observed.push_back(std::to_string(count) + ":" + text);
        });
  }

  void runFor(uint64_t usec) {
    auto exit =
        loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + usec,
                          0, [this](fcitx::EventSourceTime *, uint64_t) {
                            loop.exit();
                            return false;
                          });
    loop.exec();
  }

  fcitx::EventLoop loop;
  fcitx::InputContextManager manager;
  TestInputContext context;
  TestEngine engine;
  RecordingBackend backend;
  areca::InputScheduler scheduler;
  std::vector<std::string> observed;
};

void testInputMethodDeliveryAppliesRightAfterMarker() {
  Harness h(areca::MarkerDelivery::InputMethod);
  h.scheduler.enqueue(h.context, 'r', "r");
  // Marker vào trước, rồi xoá thêm một ký tự cho chính marker.
  assert(
      (h.context.events ==
       std::vector<std::string>{"commit:" + kMarker, "delete:2", "commit:ê"}));
  // Observer chỉ thấy phần Bamboo yêu cầu, không thấy marker.
  assert((h.observed == std::vector<std::string>{"1:ê"}));
}

void testKeysDeliveryWaitsForMarker() {
  Harness h(areca::MarkerDelivery::Keys, 10);
  const uint64_t startUsec = fcitx::now(CLOCK_MONOTONIC);
  h.scheduler.enqueue(h.context, 'r', "r");
  // Phím xoá đi kênh khác commit nên chưa được phát ngay.
  assert((h.context.events == std::vector<std::string>{"commit:" + kMarker}));
  assert(h.scheduler.rewritePending());
  h.runFor(60 * 1000);
  assert(
      (h.context.events ==
       std::vector<std::string>{"commit:" + kMarker, "delete:2", "commit:ê"}));
  assert(h.backend.appliedAtUsec >= startUsec + 10 * 1000);
  assert(!h.scheduler.rewritePending());
  assert((h.observed == std::vector<std::string>{"1:ê"}));

  // Phím không cần xoá thì không có marker.
  h.context.events.clear();
  h.scheduler.enqueue(h.context, 'a', "a");
  assert((h.context.events == std::vector<std::string>{"commit:a"}));
}

void testKeysDeliveryWithoutWaitAppliesImmediately() {
  Harness h(areca::MarkerDelivery::Keys, 0);
  h.scheduler.enqueue(h.context, 'r', "r");
  assert(
      (h.context.events ==
       std::vector<std::string>{"commit:" + kMarker, "delete:2", "commit:ê"}));
}

void testTypedDeliveryLeavesMarkerToBackend() {
  Harness h(areca::MarkerDelivery::Typed);
  h.scheduler.enqueue(h.context, 'r', "r");
  // Backend tự gõ marker trên kênh của nó, scheduler không commit.
  assert((h.context.events ==
          std::vector<std::string>{"delete:2:typed", "commit:ê"}));
}

void testInputMethodWaitsForMarkerEcho() {
  Harness h(areca::MarkerDelivery::InputMethod);
  h.context.setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
  h.context.surroundingText().setText("go", 2, 2);
  h.scheduler.enqueue(h.context, 'r', "r");
  // Ứng dụng chưa báo marker đã vào ô: chưa được xoá.
  assert((h.context.events == std::vector<std::string>{"commit:" + kMarker}));

  // Update cũ từ trước marker không đủ.
  h.context.surroundingText().setText("go", 2, 2);
  h.scheduler.notifySurroundingTextUpdated(h.context);
  assert(h.context.events.size() == 1);

  // Ứng dụng báo "go·": bắt đầu xoá ngay, không đợi hết giờ.
  h.context.surroundingText().setText("go" + kMarker, 3, 3);
  h.scheduler.notifySurroundingTextUpdated(h.context);
  assert(
      (h.context.events ==
       std::vector<std::string>{"commit:" + kMarker, "delete:2", "commit:ê"}));
  assert((h.observed == std::vector<std::string>{"1:ê"}));
}

void testInputMethodDeletesWhenEchoNeverComes() {
  Harness h(areca::MarkerDelivery::InputMethod);
  h.context.setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
  h.context.surroundingText().setText("go", 2, 2);
  h.scheduler.enqueue(h.context, 'r', "r");
  assert(h.context.events.size() == 1);
  // Mặc định đợi tối đa bằng thời gian probe surrounding (80 ms).
  h.runFor(200 * 1000);
  assert(
      (h.context.events ==
       std::vector<std::string>{"commit:" + kMarker, "delete:2", "commit:ê"}));
}

void testQueuedBackspaceForwardsWithoutComposition() {
  Harness h(areca::MarkerDelivery::InputMethod);
  // Từ đang gõ đã trống: Backspace thật xếp hàng sau phím ảo vẫn tới ứng dụng.
  h.scheduler.enqueueBackspace(h.context, true);
  h.runFor(60 * 1000);
  assert(
      (h.context.events ==
       std::vector<std::string>{"forward:BackSpace", "forward:BackSpace:up"}));
}

void testCacheCommitReplacesSelection() {
  fcitx::InputContextManager manager;
  TestInputContext context(manager);
  context.setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
  // Ô có "vie" và gợi ý "tnam" đang được chọn sau con trỏ.
  context.surroundingText().setText("vietnam", 3, 7);
  areca::updateSurroundingCacheAfterCommit(context, kMarker);
  assert(context.surroundingText().text() == "vie" + kMarker);
  assert(context.surroundingText().cursor() == 4);
  assert(context.surroundingText().anchor() == 4);
}

} // namespace

int main() {
  testInputMethodDeliveryAppliesRightAfterMarker();
  testKeysDeliveryWaitsForMarker();
  testKeysDeliveryWithoutWaitAppliesImmediately();
  testTypedDeliveryLeavesMarkerToBackend();
  testInputMethodWaitsForMarkerEcho();
  testInputMethodDeletesWhenEchoNeverComes();
  testQueuedBackspaceForwardsWithoutComposition();
  testCacheCommitReplacesSelection();
  std::cout << "Rewrite marker test passed successfully\n";
  return 0;
}
