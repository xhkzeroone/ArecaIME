#include "uinput_backspace_backend.h"

#include <linux/input.h>
#include <utility>

#include <fcitx-utils/log.h>

namespace areca {

UinputBackspaceBackend::UinputBackspaceBackend(fcitx::EventLoop &eventLoop,
                                               UinputDevice &device,
                                               DebugProvider debugProvider)
    : eventLoop_(eventLoop), device_(device),
      debugProvider_(std::move(debugProvider)) {}

UinputBackspaceBackend::~UinputBackspaceBackend() { clearPending(); }

bool UinputBackspaceBackend::isAvailable() { return device_.isAvailable(); }

ApplyStatus UinputBackspaceBackend::apply(fcitx::InputContext &inputContext,
                                          const RewritePlan &plan,
                                          RewriteDone onDone) {
  if (hasPending() || !plan.transactionId || !device_.ensureDevice()) {
    return ApplyStatus::Failed;
  }

  transactionId_ = plan.transactionId;
  inputContext_ = inputContext.watch();
  onDone_ = std::move(onDone);
  remainingBackspaces_ = plan.backspaceCount;
  sentBackspaces_ = 0;
  backspaceDelayMs_ = plan.backspaceDelayMs;
  const char *frontend = inputContext.frontend();
  afterBackspaceWaitMs_ = resolveAfterBackspaceWaitMs(frontend, plan);
  timerAccuracyUsec_ = plan.timerAccuracyUsec;
  commitText_ = plan.commitText;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-backspace start tx=" << transactionId_
                 << " backspaces=" << remainingBackspaces_
                 << " delay_ms=" << backspaceDelayMs_
                 << " after_wait_ms=" << afterBackspaceWaitMs_
                 << " frontend=" << (frontend ? frontend : "")
                 << " accuracy_us=" << timerAccuracyUsec_;
  }

  if (!remainingBackspaces_) {
    scheduleCommit();
  } else {
    sendNextBackspace();
  }
  return ApplyStatus::Pending;
}

void UinputBackspaceBackend::sendNextBackspace() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  device_.sendKeyEvent(KEY_BACKSPACE, 1); // press
  device_.sendKeyEvent(KEY_BACKSPACE, 0); // release

  --remainingBackspaces_;
  ++sentBackspaces_;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-backspace sent tx=" << transactionId_
                 << " sent=" << sentBackspaces_
                 << " remaining=" << remainingBackspaces_;
  }

  if (remainingBackspaces_) {
    scheduleNextBackspace();
  } else {
    scheduleCommit();
  }
}

void UinputBackspaceBackend::scheduleNextBackspace() {
  schedule(backspaceDelayMs_, [this]() { sendNextBackspace(); });
}

void UinputBackspaceBackend::scheduleCommit() {
  schedule(afterBackspaceWaitMs_, [this]() { commitAndComplete(); });
}

void UinputBackspaceBackend::commitAndComplete() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  if (!commitText_.empty()) {
    inputContext->commitString(commitText_);
  }

  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-backspace complete tx=" << transactionId
                 << " sent=" << sentBackspaces_ << " commit=" << commitText_;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId);
  }
}

void UinputBackspaceBackend::completeWithoutCommit() {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-backspace context lost tx="
                 << transactionId;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId);
  }
}

void UinputBackspaceBackend::schedule(uint32_t delayMs,
                                      std::function<void()> callback) {
  timer_.reset();
  const uint64_t deadline =
      fcitx::now(CLOCK_MONOTONIC) + static_cast<uint64_t>(delayMs) * 1000;
  timer_ =
      eventLoop_.addTimeEvent(CLOCK_MONOTONIC, deadline, timerAccuracyUsec_,
                              [this, callback = std::move(callback)](
                                  fcitx::EventSourceTime *, uint64_t) mutable {
                                auto completedTimer = std::move(timer_);
                                callback();
                                return false;
                              });
  if (!timer_) {
    callback();
    return;
  }
  timer_->setOneShot();
}

void UinputBackspaceBackend::clearPending() {
  timer_.reset();
  inputContext_.unwatch();
  onDone_ = {};
  transactionId_ = 0;
  remainingBackspaces_ = 0;
  sentBackspaces_ = 0;
  backspaceDelayMs_ = 0;
  afterBackspaceWaitMs_ = 0;
  timerAccuracyUsec_ = 1;
  commitText_.clear();
}

} // namespace areca
