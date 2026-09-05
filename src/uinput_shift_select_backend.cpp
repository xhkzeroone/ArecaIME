#include "uinput_shift_select_backend.h"

#include <linux/input.h>
#include <utility>

#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>

namespace areca {

UinputShiftSelectBackend::UinputShiftSelectBackend(fcitx::EventLoop &eventLoop,
                                                   UinputDevice &device,
                                                   DebugProvider debugProvider)
    : eventLoop_(eventLoop), device_(device),
      debugProvider_(std::move(debugProvider)) {}

UinputShiftSelectBackend::~UinputShiftSelectBackend() { clearPending(); }

bool UinputShiftSelectBackend::isAvailable() { return device_.isAvailable(); }

ApplyStatus UinputShiftSelectBackend::apply(fcitx::InputContext &inputContext,
                                            const RewritePlan &plan,
                                            RewriteDone onDone) {
  if (hasPending() || !plan.transactionId || !device_.ensureDevice()) {
    return ApplyStatus::Failed;
  }

  transactionId_ = plan.transactionId;
  inputContext_ = inputContext.watch();
  onDone_ = std::move(onDone);
  selectionCount_ = plan.backspaceCount;
  selectedCharacters_ = plan.backspaceCount;
  shiftSelectDelayMs_ = plan.uinputShiftSelectDelayMs;
  const char *frontend = inputContext.frontend();
  afterSelectWaitMs_ = resolveAfterUinputShiftSelectWaitMs(frontend, plan);
  timerAccuracyUsec_ = plan.timerAccuracyUsec;
  commitText_ = plan.commitText;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-shift-select start tx=" << transactionId_
                 << " select_left=" << selectionCount_
                 << " delay_ms=" << shiftSelectDelayMs_
                 << " after_wait_ms=" << afterSelectWaitMs_
                 << " frontend=" << (frontend ? frontend : "")
                 << " accuracy_us=" << timerAccuracyUsec_;
  }

  if (!selectionCount_) {
    scheduleCommit();
  } else {
    beginSelection();
  }
  return ApplyStatus::Pending;
}

void UinputShiftSelectBackend::beginSelection() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  device_.sendKeyEvent(KEY_LEFTSHIFT, 1); // Shift down
  shiftHeld_ = true;
  // Wait one shiftSelectDelayMs cycle before the first Left so the browser
  // has time to flush the Shift modifier state (needed for React/Facebook).
  schedule(shiftSelectDelayMs_, [this]() { sendNextSelectionLeft(); });
}

void UinputShiftSelectBackend::sendNextSelectionLeft() {
  if (!inputContext_.get()) {
    releaseShift();
    completeWithoutCommit();
    return;
  }

  device_.sendKeyEvent(KEY_LEFT, 1); // Left press
  device_.sendKeyEvent(KEY_LEFT, 0); // Left release
  --selectionCount_;

  if (selectionCount_) {
    schedule(shiftSelectDelayMs_, [this]() { sendNextSelectionLeft(); });
    return;
  }

  // Last Left done — delay before Shift UP.
  schedule(shiftSelectDelayMs_, [this]() { releaseShiftThenCommit(); });
}

void UinputShiftSelectBackend::releaseShiftThenCommit() {
  releaseShift();
  schedule(afterSelectWaitMs_, [this]() { commitSelectionAndComplete(); });
}

void UinputShiftSelectBackend::releaseShift() {
  if (!shiftHeld_) {
    return;
  }
  device_.sendKeyEvent(KEY_LEFTSHIFT, 0); // Shift up
  shiftHeld_ = false;
}

void UinputShiftSelectBackend::commitSelectionAndComplete() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  if (commitText_.empty()) {
    if (selectedCharacters_ > 0) {
      if (debugProvider_()) {
        FCITX_INFO() << "areca: uinput-select erase selection with backspace tx="
                     << transactionId_ << " chars=" << selectedCharacters_;
      }
      device_.sendKeyEvent(KEY_BACKSPACE, 1);
      device_.sendKeyEvent(KEY_BACKSPACE, 0);
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
    FCITX_INFO() << "areca: uinput-select 2-step commit tx=" << transactionId_
                 << " chars=" << selectedCharacters_
                 << " first=" << firstChar
                 << " remaining=" << remainingText;
  }

  inputContext->commitString(firstChar);

  if (remainingText.empty()) {
    finishTransaction();
    return;
  }

  const uint32_t charDelayMs = 5U;
  schedule(charDelayMs, [this, remainingText = std::move(remainingText)]() {
    auto *inputContext = inputContext_.get();
    if (!inputContext) {
      completeWithoutCommit();
      return;
    }
    inputContext->commitString(remainingText);
    finishTransaction();
  });
}

void UinputShiftSelectBackend::scheduleCommit() {
  schedule(afterSelectWaitMs_, [this]() { commitSelectionAndComplete(); });
}

void UinputShiftSelectBackend::completeWithoutCommit() {
  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-shift-select context lost tx="
                 << transactionId_;
  }
  finishTransaction();
}

void UinputShiftSelectBackend::finishTransaction() {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  clearPending();
  if (onDone) {
    onDone(transactionId);
  }
}

void UinputShiftSelectBackend::schedule(uint32_t delayMs,
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

void UinputShiftSelectBackend::clearPending() {
  timer_.reset();
  releaseShift();
  inputContext_.unwatch();
  onDone_ = {};
  transactionId_ = 0;
  selectionCount_ = 0;
  selectedCharacters_ = 0;
  shiftSelectDelayMs_ = 0;
  afterSelectWaitMs_ = 0;
  timerAccuracyUsec_ = 1;
  shiftHeld_ = false;
  commitText_.clear();
}

} // namespace areca
