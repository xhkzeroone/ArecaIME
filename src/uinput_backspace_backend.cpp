#include "uinput_backspace_backend.h"

#include <linux/input.h>
#include <utility>

#include <fcitx-utils/log.h>

namespace areca {
namespace {

// Backspace xác nhận và timer chạy đua trong cùng event loop. Timeout hủy bộ
// đếm để Backspace đến muộn không thể bắt đầu commit lần thứ hai.
constexpr uint32_t kBackspaceAckTimeoutMs = 20;

bool isBackspaceKey(const fcitx::KeyEvent &event) {
  return event.key().sym() == FcitxKey_BackSpace ||
         event.rawKey().sym() == FcitxKey_BackSpace;
}

} // namespace

UinputBackspaceBackend::UinputBackspaceBackend(fcitx::EventLoop &eventLoop,
                                               UinputDevice &device,
                                               AdaptiveWait &adaptiveWait,
                                               DebugProvider debugProvider)
    : eventLoop_(eventLoop), device_(device), commitPost_(eventLoop),
      adaptiveWait_(adaptiveWait), debugProvider_(std::move(debugProvider)) {}

UinputBackspaceBackend::~UinputBackspaceBackend() { clearPending(); }

bool UinputBackspaceBackend::isAvailable() { return device_.isAvailable(); }

ApplyStatus UinputBackspaceBackend::apply(fcitx::InputContext &inputContext,
                                          const RewritePlan &plan,
                                          RewriteDone onDone) {
  if (hasPending() || !plan.transactionId || !device_.ensureDevice()) {
    return ApplyStatus::Failed;
  }

  adaptiveWait_.beginTransaction();
  transactionId_ = plan.transactionId;
  inputContext_ = inputContext.watch();
  onDone_ = std::move(onDone);
  deletedCharacters_ = plan.backspaceCount;
  // Backspace dư là mốc xác nhận. Nó quay lại Fcitx nhưng bị filter nên ứng
  // dụng vẫn chỉ nhận đúng số Backspace cần xóa.
  remainingBackspaces_ = deletedCharacters_ + 1;
  sentBackspaces_ = 0;
  backspaceTracker_.reset(deletedCharacters_);
  // Kế hoạch không xóa ký tự nào không mở cửa sổ nhận Backspace xác nhận.
  backspaceAckTimedOut_ = deletedCharacters_ == 0;
  backspaceDelayMs_ = plan.backspaceDelayMs;
  const char *frontend = inputContext.frontend();
  afterBackspaceWaitMs_ = resolveAfterBackspaceWaitMs(frontend, plan);
  timerAccuracyUsec_ = plan.timerAccuracyUsec;
  commitText_ = plan.commitText;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-backspace start tx=" << transactionId_
                 << " backspaces=" << deletedCharacters_
                 << " emit_backspaces=" << remainingBackspaces_
                 << " delay_ms=" << backspaceDelayMs_
                 << " after_wait_ms=" << afterBackspaceWaitMs_
                 << " adaptive_extra_ms="
                 << adaptiveWait_.effectiveExtraWaitMs(afterBackspaceWaitMs_)
                 << " frontend=" << (frontend ? frontend : "")
                 << " accuracy_us=" << timerAccuracyUsec_;
  }

  if (!deletedCharacters_) {
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
    ackSentAtUsec_ = fcitx::now(CLOCK_MONOTONIC);
    schedule(kBackspaceAckTimeoutMs, TimerDispatch::TimerCallback, [this]() {
      if (debugProvider_()) {
        FCITX_INFO() << "areca: uinput-backspace ack timeout tx="
                     << transactionId_
                     << " seen=" << backspaceTracker_.pressesSeen()
                     << " expected=" << backspaceTracker_.expectedPresses();
      }
      backspaceAckTimedOut_ = true;
      backspaceTracker_.clear();
      adaptiveWait_.observeAckTimeout();
      scheduleCommit();
    });
  }
}

bool UinputBackspaceBackend::handleBackspace(fcitx::KeyEvent &event) {
  if (!hasPending() || backspaceAckTimedOut_ ||
      event.inputContext() != inputContext_.get() || !isBackspaceKey(event)) {
    return false;
  }

  if (event.isRelease()) {
    const bool filter = backspaceTracker_.shouldFilterRelease();
    if (debugProvider_()) {
      FCITX_INFO() << "areca: uinput-backspace release tx=" << transactionId_
                   << " seen=" << backspaceTracker_.releasesSeen()
                   << " expected=" << backspaceTracker_.expectedPresses()
                   << " action=" << (filter ? "filter" : "forward");
    }
    if (filter) {
      event.filterAndAccept();
    } else {
      event.forward();
    }
    return true;
  }

  const auto action = backspaceTracker_.observePress();
  const bool forward = action == UinputKeyAckTracker::PressAction::Forward;
  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-backspace press tx=" << transactionId_
                 << " seen=" << backspaceTracker_.pressesSeen()
                 << " expected=" << backspaceTracker_.expectedPresses()
                 << " action=" << (forward ? "forward" : "filter");
  }
  if (forward) {
    event.forward();
  } else {
    event.filterAndAccept();
  }

  if (action == UinputKeyAckTracker::PressAction::FilterAndAcknowledge) {
    if (ackSentAtUsec_ > 0) {
      const uint64_t roundtripUsec = fcitx::now(CLOCK_MONOTONIC) - ackSentAtUsec_;
      ackSentAtUsec_ = 0;
      adaptiveWait_.observeAckRoundtrip(roundtripUsec);
    }
    scheduleCommit();
  }
  return true;
}

void UinputBackspaceBackend::scheduleNextBackspace() {
  const uint32_t delayMs =
      adaptiveWait_.effectiveBackspaceDelayMs(backspaceDelayMs_);
  schedule(delayMs, TimerDispatch::TimerCallback,
           [this]() { sendNextBackspace(); });
}

void UinputBackspaceBackend::scheduleCommit() {
  const uint32_t extraWaitMs =
      adaptiveWait_.effectiveExtraWaitMs(afterBackspaceWaitMs_);
  const uint32_t effectiveWaitMs =
      adaptiveWait_.effectiveWaitMs(afterBackspaceWaitMs_);
  if (debugProvider_() && extraWaitMs > 0) {
    FCITX_INFO() << "areca: uinput-backspace adaptive commit wait tx="
                 << transactionId_ << " base_ms=" << afterBackspaceWaitMs_
                 << " extra_ms=" << extraWaitMs
                 << " effective_ms=" << effectiveWaitMs;
  }
  // Timer chỉ đánh dấu đã chờ đủ. Commit được đưa sang pha post của event loop
  // để công việc đang nghẽn được phản ánh vào độ trễ trước khi commit thật.
  schedule(effectiveWaitMs, TimerDispatch::PostEvent,
           [this, extraWaitMs]() { commitAfterAdaptiveWait(extraWaitMs); });
}

void UinputBackspaceBackend::commitAfterAdaptiveWait(
    uint32_t appliedExtraWaitMs) {
  const uint32_t currentExtraWaitMs =
      adaptiveWait_.effectiveExtraWaitMs(afterBackspaceWaitMs_);
  if (currentExtraWaitMs > appliedExtraWaitMs) {
    const uint32_t additionalWaitMs =
        currentExtraWaitMs - appliedExtraWaitMs;
    if (debugProvider_()) {
      FCITX_INFO() << "areca: uinput-backspace lag before commit tx="
                   << transactionId_ << " additional_wait_ms="
                   << additionalWaitMs;
    }
    schedule(additionalWaitMs, TimerDispatch::PostEvent,
             [this, currentExtraWaitMs]() {
               commitAfterAdaptiveWait(currentExtraWaitMs);
             });
    return;
  }
  commitAndComplete();
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
  const auto adjustment = adaptiveWait_.completeTransaction();
  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-backspace complete tx=" << transactionId
                 << " sent=" << sentBackspaces_ << " commit=" << commitText_;
    if (adjustment == AdaptiveWait::Adjustment::Decreased) {
      FCITX_INFO() << "areca: uinput-backspace adaptive wait decayed tx="
                   << transactionId << " extra_ms="
                   << adaptiveWait_.effectiveExtraWaitMs(afterBackspaceWaitMs_);
    }
  }
  clearPending();
  if (onDone) {
    onDone(transactionId, RewriteOutcome::Succeeded);
  }
}

void UinputBackspaceBackend::completeWithoutCommit() {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  adaptiveWait_.cancelTransaction();
  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-backspace context lost tx="
                 << transactionId;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId, RewriteOutcome::Succeeded);
  }
}

void UinputBackspaceBackend::schedule(uint32_t delayMs,
                                      TimerDispatch dispatch,
                                      std::function<void()> callback) {
  timer_.reset();
  const uint64_t deadline =
      fcitx::now(CLOCK_MONOTONIC) + static_cast<uint64_t>(delayMs) * 1000;
  timer_ =
      eventLoop_.addTimeEvent(CLOCK_MONOTONIC, deadline, timerAccuracyUsec_,
                              [this, deadline, dispatch,
                               callback = std::move(callback)](
                                  fcitx::EventSourceTime *, uint64_t) mutable {
                                auto completedTimer = std::move(timer_);
                                if (dispatch == TimerDispatch::PostEvent) {
                                  dispatchPostEvent(deadline,
                                                    std::move(callback));
                                } else {
                                  observeAndRun(deadline, std::move(callback));
                                }
                                return false;
                              });
  if (!timer_) {
    if (dispatch == TimerDispatch::PostEvent) {
      dispatchPostEvent(deadline, std::move(callback));
    } else {
      observeAndRun(deadline, std::move(callback));
    }
    return;
  }
  timer_->setOneShot();
}

void UinputBackspaceBackend::dispatchPostEvent(
    uint64_t deadlineUsec, std::function<void()> callback) {
  commitPost_.schedule([this, deadlineUsec, callback]() mutable {
    observeAndRun(deadlineUsec, std::move(callback));
  });
}

void UinputBackspaceBackend::observeAndRun(
    uint64_t deadlineUsec, std::function<void()> callback) {
  const uint64_t firedAtUsec = fcitx::now(CLOCK_MONOTONIC);
  const auto adjustment =
      adaptiveWait_.observeTimer(deadlineUsec, firedAtUsec);
  if (debugProvider_() && adjustment != AdaptiveWait::Adjustment::None) {
    FCITX_INFO() << "areca: uinput-backspace adaptive wait changed tx="
                 << transactionId_ << " lateness_us="
                 << (firedAtUsec > deadlineUsec ? firedAtUsec - deadlineUsec
                                                : 0)
                 << " extra_ms="
                 << adaptiveWait_.effectiveExtraWaitMs(afterBackspaceWaitMs_);
  }
  callback();
}

void UinputBackspaceBackend::clearPending() {
  timer_.reset();
  commitPost_.cancel();
  inputContext_.unwatch();
  onDone_ = {};
  transactionId_ = 0;
  deletedCharacters_ = 0;
  remainingBackspaces_ = 0;
  sentBackspaces_ = 0;
  backspaceTracker_.clear();
  backspaceDelayMs_ = 0;
  afterBackspaceWaitMs_ = 0;
  timerAccuracyUsec_ = 1;
  ackSentAtUsec_ = 0;
  backspaceAckTimedOut_ = false;
  commitText_.clear();
}

} // namespace areca
