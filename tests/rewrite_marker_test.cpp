#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "input_scheduler.h"
#include "unmarked_rewrite_backend.h"

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

// Như surrounding khi snapshot không theo kịp: không đụng tới ô, báo scheduler
// chạy lại lượt bằng backend dự phòng.
class NeedsFallbackBackend final : public areca::RewriteBackend {
public:
  explicit NeedsFallbackBackend(fcitx::EventLoop &loop) : loop_(loop) {}

  const char *name() const override { return "needs-fallback"; }
  areca::MarkerDelivery markerDelivery() const override {
    return areca::MarkerDelivery::None;
  }
  areca::ApplyStatus apply(fcitx::InputContext &,
                           const areca::RewritePlan &plan,
                           areca::RewriteDone onDone) override {
    backspaceCount = plan.backspaceCount;
    timer_ = loop_.addTimeEvent(
        CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 5000, 1,
        [transactionId = plan.transactionId,
         onDone = std::move(onDone)](fcitx::EventSourceTime *, uint64_t) {
          onDone(transactionId, areca::RewriteOutcome::NeedsFallback);
          return false;
        });
    timer_->setOneShot();
    return areca::ApplyStatus::Pending;
  }

  uint32_t backspaceCount = 0;

private:
  fcitx::EventLoop &loop_;
  std::unique_ptr<fcitx::EventSourceTime> timer_;
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
              return areca::RewriteBackendSelection{
                  selected ? selected : &backend, fallback};
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
  areca::RewriteBackend *selected = nullptr;
  areca::RewriteBackend *fallback = nullptr;
  areca::InputScheduler scheduler;
  std::vector<std::string> observed;
};

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

void testKeysDeliveryStartsOnMarkerEcho() {
  Harness h(areca::MarkerDelivery::Keys, 80);
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

void testQueuedBackspaceForwardsWithoutComposition() {
  Harness h(areca::MarkerDelivery::Keys);
  // Từ đang gõ đã trống: Backspace thật xếp hàng sau phím ảo vẫn tới ứng dụng.
  h.scheduler.enqueueBackspace(h.context, true);
  h.runFor(60 * 1000);
  assert(
      (h.context.events ==
       std::vector<std::string>{"forward:BackSpace", "forward:BackSpace:up"}));
}

void testNoMarkerDeletesExactly() {
  Harness h(areca::MarkerDelivery::None);
  h.context.setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
  h.context.surroundingText().setText("go", 2, 2);
  h.scheduler.enqueue(h.context, 'r', "r");
  // Surrounding xoá như trước khi có marker: không commit "·", xoá đúng N.
  assert(
      (h.context.events == std::vector<std::string>{"delete:1", "commit:ê"}));
  assert((h.observed == std::vector<std::string>{"1:ê"}));
}

void testUnmarkedWrapperSkipsMarker() {
  Harness h(areca::MarkerDelivery::Keys, 80);
  areca::UnmarkedRewriteBackend unmarked(h.backend, "recording-unmarked");
  h.selected = &unmarked;
  h.scheduler.enqueue(h.context, 'r', "r");
  // Backend kênh phím bọc lại chạy như trước khi có marker: không "·", không
  // chờ, xoá đúng N.
  assert(
      (h.context.events == std::vector<std::string>{"delete:1", "commit:ê"}));
  assert(!h.scheduler.rewritePending());
  assert((h.observed == std::vector<std::string>{"1:ê"}));
}

// Surrounding không xoá được ở lượt này: chạy lại bằng Backspace, kể cả commit
// marker và xoá thêm một ký tự cho nó.
void testNeedsFallbackRetriesWithMarker() {
  Harness h(areca::MarkerDelivery::Keys, 10);
  NeedsFallbackBackend primary(h.loop);
  h.selected = &primary;
  h.fallback = &h.backend;
  h.scheduler.enqueue(h.context, 'r', "r");
  assert(h.context.events.empty());
  assert(primary.backspaceCount == 1);
  h.runFor(80 * 1000);
  assert(
      (h.context.events ==
       std::vector<std::string>{"commit:" + kMarker, "delete:2", "commit:ê"}));
  assert(!h.scheduler.rewritePending());
  assert((h.observed == std::vector<std::string>{"1:ê"}));
}

} // namespace

int main() {
  testKeysDeliveryWaitsForMarker();
  testKeysDeliveryWithoutWaitAppliesImmediately();
  testTypedDeliveryLeavesMarkerToBackend();
  testKeysDeliveryStartsOnMarkerEcho();
  testQueuedBackspaceForwardsWithoutComposition();
  testNoMarkerDeletesExactly();
  testUnmarkedWrapperSkipsMarker();
  testNeedsFallbackRetriesWithMarker();
  std::cout << "Rewrite marker test passed successfully\n";
  return 0;
}
