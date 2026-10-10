#include "uinput_shift_select_backend.h"

#include <linux/input.h>
#include <utility>

#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>

namespace areca {
namespace {

// Left xác nhận và timer này chạy đua trong cùng event loop. Bên nào tới trước
// sẽ thả Shift; timeout đồng thời hủy bộ đếm để Left đến muộn không xử lý lại.
constexpr uint32_t kSelectionAckTimeoutMs = 20;

bool isLeftKey(const fcitx::KeyEvent &event) {
  return event.key().sym() == FcitxKey_Left ||
         event.rawKey().sym() == FcitxKey_Left;
}

} // namespace

UinputShiftSelectBackend::UinputShiftSelectBackend(fcitx::EventLoop &eventLoop,
                                                   UinputDevice &device,
                                                   AdaptiveWait &adaptiveWait,
                                                   DebugProvider debugProvider)
    : eventLoop_(eventLoop), device_(device), commitPost_(eventLoop),
      adaptiveWait_(&adaptiveWait), debugProvider_(std::move(debugProvider)) {}

UinputShiftSelectBackend::UinputShiftSelectBackend(fcitx::EventLoop &eventLoop,
                                                   UinputDevice &device,
                                                   DebugProvider debugProvider)
    : eventLoop_(eventLoop), device_(device), commitPost_(eventLoop),
      adaptiveWait_(nullptr), debugProvider_(std::move(debugProvider)) {}

UinputShiftSelectBackend::~UinputShiftSelectBackend() { clearPending(); }

bool UinputShiftSelectBackend::isAvailable() { return device_.isAvailable(); }

ApplyStatus UinputShiftSelectBackend::apply(fcitx::InputContext &inputContext,
                                            const RewritePlan &plan,
                                            RewriteDone onDone) {
  if (hasPending() || !plan.transactionId || !device_.ensureDevice()) {
    return ApplyStatus::Failed;
  }

  if (adaptiveWait_) {
    adaptiveWait_->beginTransaction();
  }
  transactionId_ = plan.transactionId;
  inputContext_ = inputContext.watch();
  onDone_ = std::move(onDone);
  selectedCharacters_ = plan.backspaceCount;
  // Phát dư một Left làm mốc xác nhận. Left cuối sẽ quay lại Fcitx và bị lọc,
  // nhờ vậy Shift chỉ được thả sau khi N Left chọn thật đã đi qua hàng đợi.
  selectionCount_ = selectedCharacters_ + 1;
  leftTracker_.reset(selectedCharacters_);
  leftAckTimedOut_ = false;
  shiftSelectDelayMs_ = plan.uinputShiftSelectDelayMs;
  const char *frontend = inputContext.frontend();
  afterSelectWaitMs_ = resolveAfterUinputShiftSelectWaitMs(frontend, plan);
  timerAccuracyUsec_ = plan.timerAccuracyUsec;
  commitText_ = plan.commitText;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-shift-select start tx=" << transactionId_
                 << " select_left=" << selectedCharacters_
                 << " emit_left=" << selectionCount_
                 << " delay_ms=" << shiftSelectDelayMs_
                 << " after_wait_ms=" << afterSelectWaitMs_
                 << " frontend=" << (frontend ? frontend : "")
                 << " accuracy_us=" << timerAccuracyUsec_;
  }

  if (!selectedCharacters_) {
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
  schedule(shiftSelectDelayMs_, TimerDispatch::TimerCallback,
           [this]() { sendNextSelectionLeft(); });
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
    const uint32_t effectiveDelay =
        adaptiveWait_ ? adaptiveWait_->effectiveBackspaceDelayMs(shiftSelectDelayMs_)
                      : shiftSelectDelayMs_;
    schedule(effectiveDelay, TimerDispatch::TimerCallback,
             [this]() { sendNextSelectionLeft(); });
    return;
  }

  ackSentAtUsec_ = fcitx::now(CLOCK_MONOTONIC);
  schedule(kSelectionAckTimeoutMs, TimerDispatch::TimerCallback, [this]() {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: uinput-shift-select left ack timeout tx="
                   << transactionId_ << " seen=" << leftTracker_.pressesSeen()
                   << " expected=" << leftTracker_.expectedPresses();
    }
    leftAckTimedOut_ = true;
    leftTracker_.clear();
    if (adaptiveWait_) {
      adaptiveWait_->observeAckTimeout();
    }
    releaseShiftThenCommit();
  });
}

bool UinputShiftSelectBackend::handleSelectionLeft(fcitx::KeyEvent &event) {
  if (!hasPending() || leftAckTimedOut_ ||
      event.inputContext() != inputContext_.get() || !isLeftKey(event)) {
    return false;
  }

  if (event.isRelease()) {
    const bool filter = leftTracker_.shouldFilterRelease();
    if (debugProvider_()) {
      FCITX_INFO() << "areca: uinput-shift-select left release tx="
                   << transactionId_ << " seen=" << leftTracker_.releasesSeen()
                   << " expected=" << leftTracker_.expectedPresses()
                   << " action=" << (filter ? "filter" : "forward");
    }
    if (filter) {
      event.filterAndAccept();
    } else {
      event.forward();
    }
    return true;
  }

  const auto action = leftTracker_.observePress();
  const bool forward = action == UinputKeyAckTracker::PressAction::Forward;
  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-shift-select left press tx="
                 << transactionId_ << " seen=" << leftTracker_.pressesSeen()
                 << " expected=" << leftTracker_.expectedPresses()
                 << " action=" << (forward ? "forward" : "filter");
  }

  if (forward) {
    event.forward();
  } else {
    event.filterAndAccept();
  }

  if (action == UinputKeyAckTracker::PressAction::FilterAndAcknowledge) {
    if (ackSentAtUsec_ > 0 && adaptiveWait_) {
      const uint64_t roundtripUsec = fcitx::now(CLOCK_MONOTONIC) - ackSentAtUsec_;
      ackSentAtUsec_ = 0;
      adaptiveWait_->observeAckRoundtrip(roundtripUsec);
    }
    releaseShiftThenCommit();
  }
  return true;
}

void UinputShiftSelectBackend::releaseShiftThenCommit() {
  releaseShift();
  const uint32_t effectiveWait =
      adaptiveWait_ ? adaptiveWait_->effectiveWaitMs(afterSelectWaitMs_)
                    : afterSelectWaitMs_;
  schedule(effectiveWait, TimerDispatch::PostEvent,
           [this]() { commitSelectionAndComplete(); });
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

void UinputShiftSelectBackend::scheduleCommit() {
  const uint32_t effectiveWait =
      adaptiveWait_ ? adaptiveWait_->effectiveWaitMs(afterSelectWaitMs_)
                    : afterSelectWaitMs_;
  schedule(effectiveWait, TimerDispatch::PostEvent,
           [this]() { commitSelectionAndComplete(); });
}

void UinputShiftSelectBackend::completeWithoutCommit() {
  if (adaptiveWait_) {
    adaptiveWait_->cancelTransaction();
  }
  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-shift-select context lost tx="
                 << transactionId_;
  }
  finishTransaction();
}

void UinputShiftSelectBackend::finishTransaction() {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (adaptiveWait_) {
    adaptiveWait_->completeTransaction();
  }
  clearPending();
  if (onDone) {
    onDone(transactionId, RewriteOutcome::Succeeded);
  }
}

void UinputShiftSelectBackend::schedule(uint32_t delayMs,
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

void UinputShiftSelectBackend::clearPending() {
  timer_.reset();
  commitPost_.cancel();
  releaseShift();
  inputContext_.unwatch();
  onDone_ = {};
  transactionId_ = 0;
  selectionCount_ = 0;
  selectedCharacters_ = 0;
  leftTracker_.clear();
  shiftSelectDelayMs_ = 0;
  afterSelectWaitMs_ = 0;
  timerAccuracyUsec_ = 1;
  ackSentAtUsec_ = 0;
  shiftHeld_ = false;
  leftAckTimedOut_ = false;
  commitText_.clear();
}

} // namespace areca
