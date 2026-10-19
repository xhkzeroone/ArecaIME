#include "surrounding_text_v2_backend.h"

#include <utility>

#include <fcitx-utils/event.h>
#include <fcitx-utils/log.h>

#include "surrounding_probe_match.h"
#include "surrounding_text_cache.h"

namespace areca {

SurroundingTextV2Backend::SurroundingTextV2Backend(
    fcitx::EventLoop &eventLoop, DebugProvider debugProvider,
    ProbeSucceededHandler onProbeSucceeded, ProbeFailedHandler onProbeFailed,
    FallbackBackendProvider fallbackBackendProvider, AdaptiveWait *adaptiveWait)
    : eventLoop_(eventLoop), commitPost_(eventLoop),
      adaptiveWait_(adaptiveWait), debugProvider_(std::move(debugProvider)),
      onProbeSucceeded_(std::move(onProbeSucceeded)),
      onProbeFailed_(std::move(onProbeFailed)),
      fallbackBackendProvider_(std::move(fallbackBackendProvider)) {}

SurroundingTextV2Backend::~SurroundingTextV2Backend() { clearPending(); }

ApplyStatus SurroundingTextV2Backend::apply(fcitx::InputContext &inputContext,
                                            const RewritePlan &plan,
                                            RewriteDone onDone) {
  if (hasPending() || !plan.transactionId) {
    return ApplyStatus::Failed;
  }

  if (plan.backspaceCount == 0) {
    if (!plan.commitText.empty()) {
      inputContext.commitString(plan.commitText);
      updateSurroundingCacheAfterCommitV2(inputContext, plan.commitText);
    }
    if (onDone) {
      onDone(plan.transactionId, RewriteOutcome::Succeeded);
    }
    return ApplyStatus::Completed;
  }

  if (adaptiveWait_) {
    adaptiveWait_->beginTransaction();
  }

  const auto &original = inputContext.surroundingText();
  originalProbeText_ = original.text();
  originalProbeCursor_ = original.cursor();
  auto expected = original;
  expected.deleteText(-static_cast<int>(plan.backspaceCount),
                      plan.backspaceCount);
  expectedProbeText_ = expected.text();
  expectedProbeCursor_ = expected.cursor();
  expectedProbeAnchor_ = expected.anchor();

  transactionId_ = plan.transactionId;
  inputContext_ = inputContext.watch();
  onDone_ = std::move(onDone);
  remainingDeletes_ = plan.backspaceCount;
  totalDeletes_ = plan.backspaceCount;
  const char *frontend = inputContext.frontend();
  deleteDelayMs_ = resolveSurroundingDeleteDelayMs(frontend, plan);
  afterDeleteWaitMs_ = plan.afterSurroundingDeleteWaitMs;
  timerAccuracyUsec_ = plan.timerAccuracyUsec;
  commitText_ = plan.commitText;
  // Incremental surrounding deletion must confirm its final step on every
  // rewrite. A one-time capability probe cannot guarantee that a later async
  // delete has reached the application before commit.
  isProbe_ = true;
  savedPlan_ = plan;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text-v2 start tx=" << transactionId_
                 << " total_deletes=" << totalDeletes_
                 << " delete_delay_ms=" << deleteDelayMs_
                 << " after_wait_ms=" << afterDeleteWaitMs_
                 << " verify_final_delete=" << isProbe_
                 << " frontend=" << (frontend ? frontend : "");
  }

  beginIncrementalDelete();
  return ApplyStatus::Pending;
}

void SurroundingTextV2Backend::beginIncrementalDelete() { sendNextDelete(); }

void SurroundingTextV2Backend::sendNextDelete() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  const bool probeFinalDelete = remainingDeletes_ == 1;
  --remainingDeletes_;
  if (probeFinalDelete) {
    probeDeletePending_ = true;
    scheduleProbeDeadline();
    inputContext->deleteSurroundingText(-1, 1);
    return;
  }

  inputContext->deleteSurroundingText(-1, 1);

  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text-v2 delete step tx="
                 << transactionId_ << " remaining=" << remainingDeletes_
                 << " total=" << totalDeletes_;
  }

  const uint32_t effectiveDelay =
      adaptiveWait_ ? adaptiveWait_->effectiveBackspaceDelayMs(deleteDelayMs_)
                    : deleteDelayMs_;
  schedule(effectiveDelay, TimerDispatch::TimerCallback,
           [this]() { sendNextDelete(); });
}

void SurroundingTextV2Backend::notifySurroundingTextUpdated(
    fcitx::InputContext &inputContext) {
  if (!isProbe_ || !probeDeletePending_ ||
      inputContext_.get() != &inputContext) {
    return;
  }

  ++probeUpdateCount_;

  const auto &surrounding = inputContext.surroundingText();
  if (!surroundingProbeMatches(
          surrounding.isValid(), originalProbeText_, originalProbeCursor_,
          expectedProbeText_, expectedProbeCursor_, surrounding.text(),
          surrounding.cursor(), surrounding.anchor())) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: surrounding-v2 probe ignored unmatched update"
                   << " tx=" << transactionId_
                   << " expected_text=" << expectedProbeText_
                   << " actual_text=" << surrounding.text()
                   << " expected_size=" << expectedProbeText_.size()
                   << " actual_size=" << surrounding.text().size()
                   << " actual_valid=" << surrounding.isValid()
                   << " updates=" << probeUpdateCount_;
    }
    return;
  }

  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-v2 probe succeeded tx="
                 << transactionId_;
  }
  probeDeletePending_ = false;
  timer_.reset();
  isProbe_ = false;
  if (onProbeSucceeded_) {
    onProbeSucceeded_(inputContext);
  }
  scheduleCommit();
}

void SurroundingTextV2Backend::scheduleCommit() {
  const uint32_t extraWaitMs =
      adaptiveWait_ ? adaptiveWait_->effectiveExtraWaitMs(afterDeleteWaitMs_)
                    : 0;
  const uint32_t effectiveWaitMs =
      adaptiveWait_ ? adaptiveWait_->effectiveWaitMs(afterDeleteWaitMs_)
                    : afterDeleteWaitMs_;
  if (debugProvider_() && extraWaitMs > 0) {
    FCITX_INFO() << "areca: surrounding-text-v2 adaptive commit wait tx="
                 << transactionId_ << " base_ms=" << afterDeleteWaitMs_
                 << " extra_ms=" << extraWaitMs
                 << " effective_ms=" << effectiveWaitMs;
  }
  schedule(effectiveWaitMs, TimerDispatch::PostEvent,
           [this, extraWaitMs]() { commitAfterAdaptiveWait(extraWaitMs); });
}

void SurroundingTextV2Backend::commitAfterAdaptiveWait(
    uint32_t appliedExtraWaitMs) {
  if (adaptiveWait_) {
    const uint32_t currentExtraWaitMs =
        adaptiveWait_->effectiveExtraWaitMs(afterDeleteWaitMs_);
    if (currentExtraWaitMs > appliedExtraWaitMs) {
      const uint32_t additionalWaitMs = currentExtraWaitMs - appliedExtraWaitMs;
      if (debugProvider_()) {
        FCITX_INFO() << "areca: surrounding-text-v2 lag before commit tx="
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

void SurroundingTextV2Backend::commitAndComplete() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  if (!commitText_.empty()) {
    inputContext->commitString(commitText_);
    updateSurroundingCacheAfterCommitV2(*inputContext, commitText_);
  }

  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (adaptiveWait_) {
    const auto adjustment = adaptiveWait_->completeTransaction();
    if (debugProvider_() && adjustment == AdaptiveWait::Adjustment::Decreased) {
      FCITX_INFO() << "areca: surrounding-text-v2 adaptive wait decayed tx="
                   << transactionId << " extra_ms="
                   << adaptiveWait_->effectiveExtraWaitMs(afterDeleteWaitMs_);
    }
  }
  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text-v2 complete tx=" << transactionId
                 << " commit=" << commitText_;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId, RewriteOutcome::Succeeded);
  }
}

void SurroundingTextV2Backend::completeWithoutCommit() {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (adaptiveWait_) {
    adaptiveWait_->cancelTransaction();
  }
  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text-v2 context lost tx="
                 << transactionId;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId, RewriteOutcome::Succeeded);
  }
}

void SurroundingTextV2Backend::schedule(uint32_t delayMs,
                                        TimerDispatch dispatch,
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

void SurroundingTextV2Backend::dispatchPostEvent(
    uint64_t deadlineUsec, std::function<void()> callback) {
  commitPost_.schedule(
      [this, deadlineUsec, callback = std::move(callback)]() mutable {
        observeAndRun(deadlineUsec, std::move(callback));
      });
}

void SurroundingTextV2Backend::observeAndRun(uint64_t deadlineUsec,
                                             std::function<void()> callback) {
  if (adaptiveWait_) {
    const uint64_t firedAtUsec = fcitx::now(CLOCK_MONOTONIC);
    const auto adjustment =
        adaptiveWait_->observeTimer(deadlineUsec, firedAtUsec);
    if (debugProvider_() && adjustment != AdaptiveWait::Adjustment::None) {
      FCITX_INFO() << "areca: surrounding-text-v2 adaptive wait changed tx="
                   << transactionId_ << " lateness_us="
                   << (firedAtUsec > deadlineUsec ? firedAtUsec - deadlineUsec
                                                  : 0)
                   << " extra_ms="
                   << adaptiveWait_->effectiveExtraWaitMs(afterDeleteWaitMs_);
    }
  }
  callback();
}

void SurroundingTextV2Backend::scheduleProbeDeadline() {
  const uint32_t timeoutMs = savedPlan_.surroundingProbeTimeoutMs;
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
      FCITX_INFO() << "areca: surrounding-v2 delete unconfirmed after "
                   << timeoutMs << "ms tx=" << transactionId_
                   << " updates=" << probeUpdateCount_;
    }
    handleProbeFailure();
  });
}

void SurroundingTextV2Backend::handleProbeFailure() {
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

  RewriteBackend *fallbackBackend =
      fallbackBackendProvider_ ? fallbackBackendProvider_(*inputContext)
                               : nullptr;
  RewritePlan plan = savedPlan_;
  plan.backspaceCount = remainingDeletes_ + (probeDeletePending_ ? 1U : 0U);
  plan.probeSurrounding = false;
  auto onDone = std::move(onDone_);
  auto icWatch = inputContext->watch();
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
      FCITX_INFO() << "areca: fallback from unconfirmed surrounding-v2 delete "
                      "to backend="
                   << fallbackBackend->name() << " tx=" << plan.transactionId;
    }
    const auto status = fallbackBackend->apply(*ic, plan, onDone);
    if (status == ApplyStatus::Failed && onDone) {
      onDone(plan.transactionId, RewriteOutcome::Failed);
    }
    return;
  }
  if (onDone) {
    onDone(plan.transactionId, RewriteOutcome::Failed);
  }
}

void SurroundingTextV2Backend::clearPending() {
  timer_.reset();
  commitPost_.cancel();
  inputContext_.unwatch();
  onDone_ = {};
  transactionId_ = 0;
  remainingDeletes_ = 0;
  totalDeletes_ = 0;
  deleteDelayMs_ = 0;
  afterDeleteWaitMs_ = 0;
  timerAccuracyUsec_ = 1;
  commitText_.clear();
  isProbe_ = false;
  probeDeletePending_ = false;
  savedPlan_ = {};
  originalProbeText_.clear();
  originalProbeCursor_ = 0;
  expectedProbeText_.clear();
  expectedProbeCursor_ = 0;
  expectedProbeAnchor_ = 0;
  probeUpdateCount_ = 0;
}

} // namespace areca
