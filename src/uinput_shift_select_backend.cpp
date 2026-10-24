#include "uinput_shift_select_backend.h"

#include <linux/input.h>
#include <utility>

#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>

namespace areca {
namespace {

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
  selectionCount_ = selectedCharacters_;
  awaitingShiftUpAck_ = false;
  shiftSelectDelayMs_ = plan.uinputShiftSelectDelayMs;
  ackTimeoutMs_ = plan.uinputShiftSelectAckTimeoutMs;
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
  // Cho frontend nhận Shift down trước khi phát Left.
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
    schedule(shiftSelectDelayMs_, TimerDispatch::TimerCallback,
             [this]() { sendNextSelectionLeft(); });
    return;
  }

  // Đã phát đủ N Left. Dùng chung delay trước khi thả Shift.
  schedule(shiftSelectDelayMs_, TimerDispatch::TimerCallback,
           [this]() { releaseShiftAndWaitForAck(); });
}

bool UinputShiftSelectBackend::handleKeyEvent(fcitx::KeyEvent &event) {
  if (!hasPending() || event.inputContext() != inputContext_.get()) {
    return false;
  }
  const bool isShift = event.key().sym() == FcitxKey_Shift_L ||
                       event.rawKey().sym() == FcitxKey_Shift_L;
  if (!isLeftKey(event) && !isShift) {
    return false;
  }

  // Left đến muộn vẫn đi tới app; Shift up là ACK duy nhất để chờ commit.
  event.forward();
  if (isShift && event.isRelease() && awaitingShiftUpAck_) {
    awaitingShiftUpAck_ = false;
    if (debugProvider_()) {
      FCITX_INFO() << "areca: uinput-shift-select shift-up ack tx="
                   << transactionId_;
    }
    if (ackSentAtUsec_ > 0 && adaptiveWait_) {
      adaptiveWait_->observeAckRoundtrip(fcitx::now(CLOCK_MONOTONIC) -
                                         ackSentAtUsec_);
    }
    ackSentAtUsec_ = 0;
    scheduleCommit();
  }
  return true;
}

void UinputShiftSelectBackend::releaseShiftAndWaitForAck() {
  if (!inputContext_.get()) {
    releaseShift();
    completeWithoutCommit();
    return;
  }
  awaitingShiftUpAck_ = true;
  ackSentAtUsec_ = fcitx::now(CLOCK_MONOTONIC);
  if (debugProvider_()) {
    FCITX_INFO() << "areca: uinput-shift-select shift-up sent tx="
                 << transactionId_ << " ack_timeout_ms=" << ackTimeoutMs_;
  }
  schedule(ackTimeoutMs_, TimerDispatch::TimerCallback, [this]() {
    awaitingShiftUpAck_ = false;
    ackSentAtUsec_ = 0;
    if (debugProvider_()) {
      FCITX_INFO() << "areca: uinput-shift-select shift-up ack timeout tx="
                   << transactionId_;
    }
    if (adaptiveWait_) {
      adaptiveWait_->observeAckTimeout();
    }
    scheduleCommit();
  });
  releaseShift();
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
  shiftSelectDelayMs_ = 0;
  ackTimeoutMs_ = 150;
  afterSelectWaitMs_ = 0;
  timerAccuracyUsec_ = 1;
  ackSentAtUsec_ = 0;
  shiftHeld_ = false;
  awaitingShiftUpAck_ = false;
  commitText_.clear();
}

} // namespace areca
