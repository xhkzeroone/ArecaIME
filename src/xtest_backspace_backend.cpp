#include "xtest_backspace_backend.h"

#include <utility>

#include <fcitx-utils/log.h>

namespace areca {

XTestBackspaceBackend::XTestBackspaceBackend(fcitx::EventLoop &eventLoop,
                                             XTestBackspaceDevice &device,
                                             AdaptiveWait &adaptiveWait,
                                             DebugProvider debugProvider)
    : eventLoop_(eventLoop), device_(device), commitPost_(eventLoop),
      adaptiveWait_(adaptiveWait), debugProvider_(std::move(debugProvider)) {}

XTestBackspaceBackend::~XTestBackspaceBackend() { clearPending(); }

bool XTestBackspaceBackend::isAvailable() { return device_.isAvailable(); }

ApplyStatus XTestBackspaceBackend::apply(fcitx::InputContext &inputContext,
                                         const RewritePlan &plan,
                                         RewriteDone onDone) {
  if (hasPending() || !plan.transactionId || !device_.isAvailable()) {
    return ApplyStatus::Failed;
  }

  adaptiveWait_.beginTransaction();
  transactionId_ = plan.transactionId;
  inputContext_ = inputContext.watch();
  onDone_ = std::move(onDone);
  remainingBackspaces_ = plan.backspaceCount;
  sentBackspaces_ = 0;
  backspaceDelayMs_ = plan.xtestBackspaceDelayMs;
  const char *frontend = inputContext.frontend();
  afterBackspaceWaitMs_ = resolveAfterXTestBackspaceWaitMs(frontend, plan);
  timerAccuracyUsec_ = plan.timerAccuracyUsec;
  commitText_ = plan.commitText;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: native-backspace start tx=" << transactionId_
                 << " backspaces=" << remainingBackspaces_
                 << " delay_ms=" << backspaceDelayMs_
                 << " after_wait_ms=" << afterBackspaceWaitMs_
                 << " adaptive_extra_ms="
                 << adaptiveWait_.effectiveExtraWaitMs(afterBackspaceWaitMs_)
                 << " frontend=" << (frontend ? frontend : "")
                 << " accuracy_us=" << timerAccuracyUsec_;
  }

  if (!remainingBackspaces_) {
    scheduleCommit();
  } else if (!sendNextBackspace(false)) {
    return ApplyStatus::Failed;
  }
  return ApplyStatus::Pending;
}

bool XTestBackspaceBackend::sendNextBackspace(bool notifyFailure) {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return true;
  }

  if (!device_.sendBackspace()) {
    failTransaction(notifyFailure);
    return false;
  }
  --remainingBackspaces_;
  ++sentBackspaces_;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: native-backspace sent tx=" << transactionId_
                 << " sent=" << sentBackspaces_
                 << " remaining=" << remainingBackspaces_;
  }

  if (remainingBackspaces_) {
    scheduleNextBackspace();
  } else {
    scheduleCommit();
  }
  return true;
}

void XTestBackspaceBackend::failTransaction(bool notifyFailure) {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  adaptiveWait_.cancelTransaction();
  FCITX_ERROR() << "areca: native-backspace send failed tx=" << transactionId
                << " sent=" << sentBackspaces_
                << " remaining=" << remainingBackspaces_;
  clearPending();
  if (notifyFailure && onDone) {
    onDone(transactionId, RewriteOutcome::Failed);
  }
}

void XTestBackspaceBackend::scheduleNextBackspace() {
  const uint32_t delayMs =
      adaptiveWait_.effectiveBackspaceDelayMs(backspaceDelayMs_);
  schedule(delayMs, TimerDispatch::TimerCallback,
           [this]() { sendNextBackspace(); });
}

void XTestBackspaceBackend::scheduleCommit() {
  const uint32_t extraWaitMs =
      adaptiveWait_.effectiveExtraWaitMs(afterBackspaceWaitMs_);
  const uint32_t effectiveWaitMs =
      adaptiveWait_.effectiveWaitMs(afterBackspaceWaitMs_);
  if (debugProvider_() && extraWaitMs > 0) {
    FCITX_INFO() << "areca: native-backspace adaptive commit wait tx="
                 << transactionId_ << " base_ms=" << afterBackspaceWaitMs_
                 << " extra_ms=" << extraWaitMs
                 << " effective_ms=" << effectiveWaitMs;
  }
  schedule(effectiveWaitMs, TimerDispatch::PostEvent,
           [this, extraWaitMs]() { commitAfterAdaptiveWait(extraWaitMs); });
}

void XTestBackspaceBackend::commitAfterAdaptiveWait(
    uint32_t appliedExtraWaitMs) {
  const uint32_t currentExtraWaitMs =
      adaptiveWait_.effectiveExtraWaitMs(afterBackspaceWaitMs_);
  if (currentExtraWaitMs > appliedExtraWaitMs) {
    const uint32_t additionalWaitMs =
        currentExtraWaitMs - appliedExtraWaitMs;
    if (debugProvider_()) {
      FCITX_INFO() << "areca: native-backspace lag before commit tx="
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

void XTestBackspaceBackend::commitAndComplete() {
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
    FCITX_INFO() << "areca: native-backspace complete tx=" << transactionId
                 << " sent=" << sentBackspaces_ << " commit=" << commitText_;
    if (adjustment == AdaptiveWait::Adjustment::Decreased) {
      FCITX_INFO() << "areca: native-backspace adaptive wait decayed tx="
                   << transactionId << " extra_ms="
                   << adaptiveWait_.effectiveExtraWaitMs(afterBackspaceWaitMs_);
    }
  }
  clearPending();
  if (onDone) {
    onDone(transactionId, RewriteOutcome::Succeeded);
  }
}

void XTestBackspaceBackend::completeWithoutCommit() {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  adaptiveWait_.cancelTransaction();
  if (debugProvider_()) {
    FCITX_INFO() << "areca: native-backspace context lost tx="
                 << transactionId;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId, RewriteOutcome::Succeeded);
  }
}

void XTestBackspaceBackend::schedule(uint32_t delayMs,
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

void XTestBackspaceBackend::dispatchPostEvent(
    uint64_t deadlineUsec, std::function<void()> callback) {
  commitPost_.schedule([this, deadlineUsec, callback]() mutable {
    observeAndRun(deadlineUsec, std::move(callback));
  });
}

void XTestBackspaceBackend::observeAndRun(
    uint64_t deadlineUsec, std::function<void()> callback) {
  const uint64_t firedAtUsec = fcitx::now(CLOCK_MONOTONIC);
  const auto adjustment =
      adaptiveWait_.observeTimer(deadlineUsec, firedAtUsec);
  if (debugProvider_() && adjustment != AdaptiveWait::Adjustment::None) {
    FCITX_INFO() << "areca: native-backspace adaptive wait changed tx="
                 << transactionId_ << " lateness_us="
                 << (firedAtUsec > deadlineUsec ? firedAtUsec - deadlineUsec
                                                : 0)
                 << " extra_ms="
                 << adaptiveWait_.effectiveExtraWaitMs(afterBackspaceWaitMs_);
  }
  callback();
}

void XTestBackspaceBackend::clearPending() {
  timer_.reset();
  commitPost_.cancel();
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
