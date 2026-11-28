#include "surrounding_text_backend.h"

#include <algorithm>
#include <utility>

#include <fcitx-utils/event.h>
#include <fcitx-utils/log.h>

#include "surrounding_probe_match.h"

namespace areca {

SurroundingTextBackend::SurroundingTextBackend(
    fcitx::EventLoop &eventLoop, DebugProvider debugProvider,
    ProbeSucceededHandler onProbeSucceeded, ProbeFailedHandler onProbeFailed,
    FallbackBackendProvider fallbackBackendProvider, AdaptiveWait *adaptiveWait)
    : eventLoop_(eventLoop), commitPost_(eventLoop),
      adaptiveWait_(adaptiveWait), debugProvider_(std::move(debugProvider)),
      onProbeSucceeded_(std::move(onProbeSucceeded)),
      onProbeFailed_(std::move(onProbeFailed)),
      fallbackBackendProvider_(std::move(fallbackBackendProvider)) {}

SurroundingTextBackend::~SurroundingTextBackend() { clearPending(); }

ApplyStatus SurroundingTextBackend::apply(fcitx::InputContext &inputContext,
                                          const RewritePlan &plan,
                                          RewriteDone onDone) {
  if (hasPending() || !plan.transactionId) {
    return ApplyStatus::Failed;
  }

  if (plan.backspaceCount == 0) {
    if (!plan.commitText.empty()) {
      inputContext.commitString(plan.commitText);
    }
    if (onDone) {
      onDone(plan.transactionId, RewriteOutcome::Succeeded);
    }
    return ApplyStatus::Completed;
  }

  // Lưu transaction trước mọi tác dụng phụ: frontend có thể gửi
  // SurroundingTextUpdated đồng bộ ngay trong lệnh xoá.
  transactionId_ = plan.transactionId;
  inputContext_ = inputContext.watch();
  onDone_ = std::move(onDone);
  waitMs_ = plan.surroundingWaitMs;
  timerAccuracyUsec_ = plan.timerAccuracyUsec;
  commitText_ = plan.commitText;
  savedPlan_ = plan;
  expectedTail_ = plan.expectedTextBeforeCursor;
  pollMs_ = std::max<uint32_t>(1, plan.surroundingPollMs);
  checkStartUsec_ = fcitx::now(CLOCK_MONOTONIC);
  checkDeadlineUsec_ =
      checkStartUsec_ +
      static_cast<uint64_t>(plan.surroundingCheckWaitMs) * 1000;
  pollCount_ = 0;

  if (debugProvider_()) {
    const char *frontend = inputContext.frontend();
    FCITX_INFO() << "areca: surrounding-text start tx=" << transactionId_
                 << " delete=" << plan.backspaceCount
                 << " probe=" << plan.probeSurrounding
                 << " expected_tail=" << expectedTail_
                 << " check_wait_ms=" << plan.surroundingCheckWaitMs
                 << " poll_ms=" << pollMs_
                 << " frontend=" << (frontend ? frontend : "");
  }

  pollSnapshot();
  return ApplyStatus::Pending;
}

void SurroundingTextBackend::pollSnapshot() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }
  ++pollCount_;
  const auto &surrounding = inputContext->surroundingText();
  const bool matched = surroundingSnapshotEndsWith(
      surrounding.isValid(), surrounding.text(), surrounding.cursor(),
      surrounding.anchor(), expectedTail_);
  const uint64_t nowUsec = fcitx::now(CLOCK_MONOTONIC);
  if (matched || nowUsec >= checkDeadlineUsec_) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: surrounding-text check tx=" << transactionId_
                   << " matched=" << matched << " polls=" << pollCount_
                   << " waited_us=" << (nowUsec - checkStartUsec_)
                   << " expected_tail=" << expectedTail_
                   << " actual_text=" << surrounding.text()
                   << " cursor=" << surrounding.cursor()
                   << " anchor=" << surrounding.anchor();
    }
    if (matched) {
      beginRewrite();
    } else {
      fallbackToOtherBackend();
    }
    return;
  }
  const uint64_t nextUsec = std::min(
      checkDeadlineUsec_, nowUsec + static_cast<uint64_t>(pollMs_) * 1000);
  timer_ =
      eventLoop_.addTimeEvent(CLOCK_MONOTONIC, nextUsec, timerAccuracyUsec_,
                              [this](fcitx::EventSourceTime *, uint64_t) {
                                auto firedTimer = std::move(timer_);
                                pollSnapshot();
                                return false;
                              });
  if (!timer_) {
    fallbackToOtherBackend();
    return;
  }
  timer_->setOneShot();
}

void SurroundingTextBackend::beginRewrite() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }
  if (!savedPlan_.probeSurrounding) {
    commitPost_.schedule([this]() { deleteAndCommit(); });
    return;
  }

  // Probe: xoá rồi chờ ứng dụng xác nhận đã xoá mới commit, không xác nhận thì
  // đổi backend.
  if (adaptiveWait_) {
    adaptiveWait_->beginTransaction();
  }
  const auto &original = inputContext->surroundingText();
  originalProbeText_ = original.text();
  originalProbeCursor_ = original.cursor();
  auto expected = original;
  expected.deleteText(-static_cast<int>(savedPlan_.backspaceCount),
                      savedPlan_.backspaceCount);
  expectedProbeText_ = expected.text();
  expectedProbeCursor_ = expected.cursor();
  expectedProbeAnchor_ = expected.anchor();
  isProbe_ = true;

  inputContext->deleteSurroundingText(
      -static_cast<int>(savedPlan_.backspaceCount), savedPlan_.backspaceCount);
  // Update đồng bộ trong lệnh xoá có thể đã xác nhận probe và hẹn commit.
  if (isProbe_) {
    scheduleProbeDeadline();
  }
}

void SurroundingTextBackend::deleteAndCommit() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }
  // Snapshot vừa khớp nên frontend đổi số ký tự ra byte đúng. Không tự sửa
  // snapshot: lượt sau phải đợi ứng dụng báo chữ mới.
  inputContext->deleteSurroundingText(
      -static_cast<int>(savedPlan_.backspaceCount), savedPlan_.backspaceCount);
  if (!commitText_.empty()) {
    inputContext->commitString(commitText_);
  }
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text complete tx=" << transactionId
                 << " commit=" << commitText_;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId, RewriteOutcome::Succeeded);
  }
}

void SurroundingTextBackend::notifySurroundingTextUpdated(
    fcitx::InputContext &inputContext) {
  if (!isProbe_) {
    return;
  }
  auto *ic = inputContext_.get();
  if (!ic || ic != &inputContext) {
    return;
  }

  ++probeUpdateCount_;

  const auto &surrounding = inputContext.surroundingText();
  if (!surroundingProbeMatches(surrounding.isValid(), originalProbeText_,
                               originalProbeCursor_, expectedProbeText_,
                               expectedProbeCursor_, surrounding.text(),
                               surrounding.cursor(), surrounding.anchor())) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: surrounding probe ignored unmatched update"
                   << " tx=" << transactionId_
                   << " expected_text=" << expectedProbeText_
                   << " actual_text=" << surrounding.text()
                   << " expected_size=" << expectedProbeText_.size()
                   << " actual_size=" << surrounding.text().size()
                   << " actual_valid=" << surrounding.isValid()
                   << " expected_cursor=" << expectedProbeCursor_
                   << " actual_cursor=" << surrounding.cursor()
                   << " expected_anchor=" << expectedProbeAnchor_
                   << " actual_anchor=" << surrounding.anchor();
      FCITX_INFO() << "areca: surrounding probe update count="
                   << probeUpdateCount_ << " tx=" << transactionId_;
    }
    return;
  }

  if (debugProvider_()) {
    FCITX_INFO()
        << "areca: surrounding probe succeeded via SurroundingTextUpdated event"
        << " tx=" << transactionId_;
  }

  isProbe_ = false;
  timer_.reset();

  if (onProbeSucceeded_) {
    onProbeSucceeded_(inputContext);
  }
  // The event already installed the confirmed surrounding snapshot in the
  // input context. Mutating it again here would delete the same text twice.
  scheduleCommit();
}

void SurroundingTextBackend::scheduleCommit() {
  const uint32_t extraWaitMs =
      adaptiveWait_ ? adaptiveWait_->effectiveExtraWaitMs(waitMs_) : 0;
  const uint32_t effectiveWaitMs =
      adaptiveWait_ ? adaptiveWait_->effectiveWaitMs(waitMs_) : waitMs_;
  if (debugProvider_() && extraWaitMs > 0) {
    FCITX_INFO() << "areca: surrounding-text adaptive commit wait tx="
                 << transactionId_ << " base_ms=" << waitMs_
                 << " extra_ms=" << extraWaitMs
                 << " effective_ms=" << effectiveWaitMs;
  }
  schedule(effectiveWaitMs, TimerDispatch::PostEvent,
           [this, extraWaitMs]() { commitAfterAdaptiveWait(extraWaitMs); });
}

void SurroundingTextBackend::commitAfterAdaptiveWait(
    uint32_t appliedExtraWaitMs) {
  if (adaptiveWait_) {
    const uint32_t currentExtraWaitMs =
        adaptiveWait_->effectiveExtraWaitMs(waitMs_);
    if (currentExtraWaitMs > appliedExtraWaitMs) {
      const uint32_t additionalWaitMs = currentExtraWaitMs - appliedExtraWaitMs;
      if (debugProvider_()) {
        FCITX_INFO() << "areca: surrounding-text lag before commit tx="
                     << transactionId_
                     << " additional_wait_ms=" << additionalWaitMs;
      }
      schedule(additionalWaitMs, TimerDispatch::PostEvent,
               [this, currentExtraWaitMs]() {
                 commitAfterAdaptiveWait(currentExtraWaitMs);
               });
      return;
    }
  }
  commitAndComplete();
}

void SurroundingTextBackend::commitAndComplete() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  // Không tự thêm chữ vào snapshot: lượt sau phải đợi ứng dụng báo chữ mới.
  if (!commitText_.empty()) {
    inputContext->commitString(commitText_);
  }

  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (adaptiveWait_) {
    const auto adjustment = adaptiveWait_->completeTransaction();
    if (debugProvider_() && adjustment == AdaptiveWait::Adjustment::Decreased) {
      FCITX_INFO() << "areca: surrounding-text adaptive wait decayed tx="
                   << transactionId << " extra_ms="
                   << adaptiveWait_->effectiveExtraWaitMs(waitMs_);
    }
  }
  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text complete tx=" << transactionId
                 << " commit=" << commitText_;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId, RewriteOutcome::Succeeded);
  }
}

void SurroundingTextBackend::completeWithoutCommit() {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (adaptiveWait_) {
    adaptiveWait_->cancelTransaction();
  }
  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text context lost tx=" << transactionId;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId, RewriteOutcome::Succeeded);
  }
}

void SurroundingTextBackend::schedule(uint32_t delayMs, TimerDispatch dispatch,
                                      std::function<void()> callback) {
  timer_.reset();
  const uint64_t deadline =
      fcitx::now(CLOCK_MONOTONIC) + static_cast<uint64_t>(delayMs) * 1000;
  timer_ = eventLoop_.addTimeEvent(
      CLOCK_MONOTONIC, deadline, timerAccuracyUsec_,
      [this, deadline, dispatch, callback = std::move(callback)](
          fcitx::EventSourceTime *, uint64_t) mutable {
        auto completedTimer = std::move(timer_);
        if (dispatch == TimerDispatch::PostEvent) {
          dispatchPostEvent(deadline, std::move(callback));
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

void SurroundingTextBackend::dispatchPostEvent(uint64_t deadlineUsec,
                                               std::function<void()> callback) {
  commitPost_.schedule(
      [this, deadlineUsec, callback = std::move(callback)]() mutable {
        observeAndRun(deadlineUsec, std::move(callback));
      });
}

void SurroundingTextBackend::observeAndRun(uint64_t deadlineUsec,
                                           std::function<void()> callback) {
  if (adaptiveWait_) {
    const uint64_t firedAtUsec = fcitx::now(CLOCK_MONOTONIC);
    const auto adjustment =
        adaptiveWait_->observeTimer(deadlineUsec, firedAtUsec);
    if (debugProvider_() && adjustment != AdaptiveWait::Adjustment::None) {
      FCITX_INFO() << "areca: surrounding-text adaptive wait changed tx="
                   << transactionId_ << " lateness_us="
                   << (firedAtUsec > deadlineUsec ? firedAtUsec - deadlineUsec
                                                  : 0)
                   << " extra_ms="
                   << adaptiveWait_->effectiveExtraWaitMs(waitMs_);
    }
  }
  callback();
}

void SurroundingTextBackend::scheduleProbeDeadline() {
  const uint32_t timeoutMs = savedPlan_.surroundingProbeTimeoutMs;
  // Nếu không nhận được SurroundingTextUpdated event
  // trong thời gian này thì đánh giá ứng dụng không hỗ trợ và chuyển backend.
  schedule(timeoutMs, TimerDispatch::TimerCallback, [this, timeoutMs]() {
    if (!isProbe_) {
      return;
    }
    auto *inputContext = inputContext_.get();
    if (!inputContext) {
      completeWithoutCommit();
      return;
    }
    if (debugProvider_()) {
      FCITX_INFO() << "areca: surrounding delete unconfirmed after "
                   << timeoutMs << "ms tx=" << transactionId_
                   << " updates=" << probeUpdateCount_;
    }
    handleProbeFailure();
  });
}

void SurroundingTextBackend::handleProbeFailure() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  if (adaptiveWait_) {
    adaptiveWait_->cancelTransaction();
  }

  if (onProbeFailed_) {
    onProbeFailed_(*inputContext);
  }
  fallbackToOtherBackend();
}

void SurroundingTextBackend::fallbackToOtherBackend() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  RewriteBackend *fallbackBackend =
      fallbackBackendProvider_ ? fallbackBackendProvider_(*inputContext)
                               : nullptr;

  RewritePlan plan = savedPlan_;
  // Chưa có gì bị xoá bằng surrounding: backend phát phím fallback xoá đúng N
  // theo ACK và timing riêng của nó.
  plan.probeSurrounding = false;
  auto onDone = std::move(onDone_);
  fcitx::TrackableObjectReference<fcitx::InputContext> icWatch =
      inputContext->watch();

  clearPending();

  auto *ic = icWatch.get();
  if (!ic) {
    if (onDone) {
      onDone(plan.transactionId, RewriteOutcome::Failed);
    }
    return;
  }

  if (fallbackBackend) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: surrounding-text fallback backend="
                   << fallbackBackend->name() << " tx=" << plan.transactionId;
    }
    // Keep our callback until apply() confirms ownership. A backend returning
    // Failed is not required to retain or invoke the callback.
    const auto status = fallbackBackend->apply(*ic, plan, onDone);
    if (status == ApplyStatus::Failed) {
      if (debugProvider_()) {
        FCITX_INFO() << "areca: fallback backend failed tx="
                     << plan.transactionId;
      }
      if (onDone) {
        onDone(plan.transactionId, RewriteOutcome::Failed);
      }
    }
    return;
  }

  if (onDone) {
    onDone(plan.transactionId, RewriteOutcome::Failed);
  }
}

void SurroundingTextBackend::clearPending() {
  timer_.reset();
  commitPost_.cancel();
  inputContext_.unwatch();
  onDone_ = {};
  transactionId_ = 0;
  waitMs_ = 0;
  timerAccuracyUsec_ = 1;
  commitText_.clear();
  isProbe_ = false;
  savedPlan_ = {};
  originalProbeText_.clear();
  originalProbeCursor_ = 0;
  expectedProbeText_.clear();
  expectedProbeCursor_ = 0;
  expectedProbeAnchor_ = 0;
  probeUpdateCount_ = 0;
  expectedTail_.clear();
  pollMs_ = 1;
  checkStartUsec_ = 0;
  checkDeadlineUsec_ = 0;
  pollCount_ = 0;
}

} // namespace areca
