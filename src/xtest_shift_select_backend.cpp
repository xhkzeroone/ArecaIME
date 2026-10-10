#include "xtest_shift_select_backend.h"

#include <utility>

#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>

namespace areca {

XTestShiftSelectBackend::XTestShiftSelectBackend(
    fcitx::EventLoop &eventLoop, XTestBackspaceDevice &device,
    AdaptiveWait &adaptiveWait, DebugProvider debugProvider)
    : eventLoop_(eventLoop), device_(device), commitPost_(eventLoop),
      adaptiveWait_(adaptiveWait), debugProvider_(std::move(debugProvider)) {}

XTestShiftSelectBackend::~XTestShiftSelectBackend() { clearPending(); }

ApplyStatus XTestShiftSelectBackend::apply(fcitx::InputContext &inputContext,
                                          const RewritePlan &plan,
                                          RewriteDone onDone) {
  if (hasPending() || !plan.transactionId || !device_.isAvailable()) {
    return ApplyStatus::Failed;
  }

  adaptiveWait_.beginTransaction();
  transactionId_ = plan.transactionId;
  inputContext_ = inputContext.watch();
  onDone_ = std::move(onDone);
  selectedCharacters_ = plan.backspaceCount;
  remainingLefts_ = selectedCharacters_;
  shiftSelectDelayMs_ = plan.uinputShiftSelectDelayMs;
  const char *frontend = inputContext.frontend();
  afterSelectWaitMs_ = resolveAfterUinputShiftSelectWaitMs(frontend, plan);
  timerAccuracyUsec_ = plan.timerAccuracyUsec;
  commitText_ = plan.commitText;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: native-shift-select start tx=" << transactionId_
                 << " chars=" << selectedCharacters_
                 << " delay_ms=" << shiftSelectDelayMs_
                 << " after_wait_ms=" << afterSelectWaitMs_
                 << " frontend=" << (frontend ? frontend : "")
                 << " accuracy_us=" << timerAccuracyUsec_;
  }

  if (!selectedCharacters_) {
    scheduleCommit();
  } else if (!beginSelection(false)) {
    return ApplyStatus::Failed;
  }
  return ApplyStatus::Pending;
}

bool XTestShiftSelectBackend::beginSelection(bool notifyFailure) {
  shiftHeld_ = true;
  if (!device_.sendShift(true)) {
    shiftHeld_ = false;
    failTransaction(notifyFailure);
    return false;
  }
  return sendNextSelectionLeft(notifyFailure);
}

bool XTestShiftSelectBackend::sendNextSelectionLeft(bool notifyFailure) {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    releaseShift();
    completeWithoutCommit();
    return true;
  }

  if (!device_.sendLeft()) {
    releaseShift();
    failTransaction(notifyFailure);
    return false;
  }
  --remainingLefts_;

  if (remainingLefts_ > 0) {
    const uint32_t delayMs =
        adaptiveWait_.effectiveBackspaceDelayMs(shiftSelectDelayMs_);
    schedule(delayMs, TimerDispatch::TimerCallback,
             [this]() { sendNextSelectionLeft(true); });
    return true;
  }

  releaseShiftThenCommit();
  return true;
}

void XTestShiftSelectBackend::releaseShiftThenCommit() {
  releaseShift();
  scheduleCommit();
}

void XTestShiftSelectBackend::releaseShift() {
  if (!shiftHeld_) {
    return;
  }
  device_.sendShift(false);
  shiftHeld_ = false;
}

void XTestShiftSelectBackend::scheduleCommit() {
  const uint32_t effectiveWait =
      adaptiveWait_.effectiveWaitMs(afterSelectWaitMs_);
  schedule(effectiveWait, TimerDispatch::PostEvent,
           [this]() { commitSelectionAndComplete(); });
}

void XTestShiftSelectBackend::commitSelectionAndComplete() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  if (commitText_.empty()) {
    if (selectedCharacters_ > 0) {
      if (debugProvider_()) {
        FCITX_INFO() << "areca: native-shift-select erase selection tx="
                     << transactionId_ << " chars=" << selectedCharacters_;
      }
      device_.sendBackspace();
    }
    finishTransaction();
    return;
  }

  auto it = commitText_.begin();
  uint32_t codepoint = 0;
  auto nextIt = fcitx::utf8::getNextChar(it, commitText_.end(), &codepoint);
  std::string firstChar(it, nextIt);
  std::string remainingText(nextIt, commitText_.end());

  if (debugProvider_()) {
    FCITX_INFO() << "areca: native-shift-select 2-step commit tx="
                 << transactionId_ << " chars=" << selectedCharacters_
                 << " first=" << firstChar << " remaining=" << remainingText;
  }

  inputContext->commitString(firstChar);

  if (remainingText.empty()) {
    finishTransaction();
    return;
  }

  const uint32_t charDelayMs = 5U;
  schedule(
      charDelayMs, TimerDispatch::PostEvent,
      [this, remainingText = std::move(remainingText)]() {
        auto *inputContext = inputContext_.get();
        if (!inputContext) {
          completeWithoutCommit();
          return;
        }
        inputContext->commitString(remainingText);
        finishTransaction();
      });
}

void XTestShiftSelectBackend::completeWithoutCommit() {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  adaptiveWait_.cancelTransaction();
  if (debugProvider_()) {
    FCITX_INFO() << "areca: native-shift-select context lost tx="
                 << transactionId;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId, RewriteOutcome::Succeeded);
  }
}

void XTestShiftSelectBackend::failTransaction(bool notifyFailure) {
  if (debugProvider_()) {
    FCITX_INFO() << "areca: native-shift-select failed tx=" << transactionId_;
  }
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  adaptiveWait_.cancelTransaction();
  clearPending();
  if (notifyFailure && onDone) {
    onDone(transactionId, RewriteOutcome::Failed);
  }
}

void XTestShiftSelectBackend::finishTransaction() {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  adaptiveWait_.completeTransaction();
  clearPending();
  if (onDone) {
    onDone(transactionId, RewriteOutcome::Succeeded);
  }
}

void XTestShiftSelectBackend::schedule(uint32_t delayMs,
                                       TimerDispatch dispatch,
                                       std::function<void()> callback) {
  timer_.reset();
  const uint64_t deadline =
      fcitx::now(CLOCK_MONOTONIC) + static_cast<uint64_t>(delayMs) * 1000;
  timer_ =
      eventLoop_.addTimeEvent(CLOCK_MONOTONIC, deadline, timerAccuracyUsec_,
                              [this, dispatch, callback = std::move(callback)](
                                  fcitx::EventSourceTime *, uint64_t) mutable {
                                auto completedTimer = std::move(timer_);
                                if (dispatch == TimerDispatch::PostEvent) {
                                  commitPost_.schedule(std::move(callback));
                                } else {
                                  callback();
                                }
                                return false;
                              });
  if (!timer_) {
    if (dispatch == TimerDispatch::PostEvent) {
      commitPost_.schedule(std::move(callback));
    } else {
      callback();
    }
    return;
  }
  timer_->setOneShot();
}

void XTestShiftSelectBackend::clearPending() {
  timer_.reset();
  commitPost_.cancel();
  releaseShift();
  inputContext_.unwatch();
  onDone_ = {};
  transactionId_ = 0;
  selectedCharacters_ = 0;
  remainingLefts_ = 0;
  shiftSelectDelayMs_ = 0;
  afterSelectWaitMs_ = 0;
  timerAccuracyUsec_ = 1;
  shiftHeld_ = false;
  commitText_.clear();
}

} // namespace areca
