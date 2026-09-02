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
  backspaceDelayMs_ = plan.backspaceDelayMs;
  const char *frontend = inputContext.frontend();
  afterBackspaceWaitMs_ = resolveAfterBackspaceWaitMs(frontend, plan);
  timerAccuracyUsec_ = plan.timerAccuracyUsec;
  commitText_ = plan.commitText;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-shift-select start tx=" << transactionId_
                 << " select_left=" << selectionCount_
                 << " delay_ms=" << backspaceDelayMs_
                 << " after_wait_ms=" << afterBackspaceWaitMs_
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
  // Wait one backspaceDelayMs cycle before the first Left so the browser
  // has time to flush the Shift modifier state (needed for React/Facebook).
  schedule(backspaceDelayMs_, [this]() { sendNextSelectionLeft(); });
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
    schedule(backspaceDelayMs_, [this]() { sendNextSelectionLeft(); });
    return;
  }

  // Last Left done — delay before Shift UP.
  schedule(backspaceDelayMs_, [this]() { releaseShiftThenCommit(); });
}

void UinputShiftSelectBackend::releaseShiftThenCommit() {
  releaseShift();
  // Delay after Shift UP before committing (10ms).
  const uint32_t waitMs = 10U;
  schedule(waitMs, [this]() { commitSelectionAndComplete(); });
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

  commitChars_.clear();
  auto it = commitText_.begin();
  while (it != commitText_.end()) {
    auto start = it;
    uint32_t codepoint = 0;
    it = fcitx::utf8::getNextChar(it, commitText_.end(), &codepoint);
    commitChars_.emplace_back(start, it);
  }

  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-select split commit (20ms) tx="
                 << transactionId_ << " chars=" << selectedCharacters_
                 << " commit=" << commitText_
                 << " split_count=" << commitChars_.size();
  }

  if (commitChars_.empty()) {
    const uint64_t transactionId = transactionId_;
    auto onDone = std::move(onDone_);
    clearPending();
    if (onDone) {
      onDone(transactionId);
    }
  } else {
    commitNextChar(0);
  }
}

void UinputShiftSelectBackend::commitNextChar(size_t index) {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  inputContext->commitString(commitChars_[index]);

  if (index + 1 < commitChars_.size()) {
    const uint32_t charDelayMs = 20U;
    schedule(charDelayMs, [this, index]() { commitNextChar(index + 1); });
  } else {
    const uint64_t transactionId = transactionId_;
    auto onDone = std::move(onDone_);
    clearPending();
    if (onDone) {
      onDone(transactionId);
    }
  }
}

void UinputShiftSelectBackend::scheduleCommit() {
  schedule(afterBackspaceWaitMs_, [this]() { commitSelectionAndComplete(); });
}

void UinputShiftSelectBackend::completeWithoutCommit() {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-shift-select context lost tx=" << transactionId;
  }
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
  backspaceDelayMs_ = 0;
  afterBackspaceWaitMs_ = 0;
  timerAccuracyUsec_ = 1;
  shiftHeld_ = false;
  commitText_.clear();
  commitChars_.clear();
}

} // namespace areca
