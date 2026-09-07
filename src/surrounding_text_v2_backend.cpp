#include "surrounding_text_v2_backend.h"

#include <utility>

#include <fcitx-utils/event.h>
#include <fcitx-utils/log.h>

#include "surrounding_text_cache.h"

namespace areca {

SurroundingTextV2Backend::SurroundingTextV2Backend(fcitx::EventLoop &eventLoop,
                                                   DebugProvider debugProvider)
    : eventLoop_(eventLoop), debugProvider_(std::move(debugProvider)) {}

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
      onDone(plan.transactionId);
    }
    return ApplyStatus::Completed;
  }

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

  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text-v2 start tx=" << transactionId_
                 << " total_deletes=" << totalDeletes_
                 << " delete_delay_ms=" << deleteDelayMs_
                 << " after_wait_ms=" << afterDeleteWaitMs_
                 << " frontend=" << (frontend ? frontend : "");
  }

  beginIncrementalDelete();
  return ApplyStatus::Pending;
}

void SurroundingTextV2Backend::beginIncrementalDelete() {
  sendNextDelete();
}

void SurroundingTextV2Backend::sendNextDelete() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  inputContext->deleteSurroundingText(-1, 1);
  updateSurroundingCacheAfterDeleteV2(*inputContext, -1, 1);
  --remainingDeletes_;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text-v2 delete step tx="
                 << transactionId_ << " remaining=" << remainingDeletes_
                 << " total=" << totalDeletes_;
  }

  if (remainingDeletes_ > 0) {
    schedule(deleteDelayMs_, [this]() { sendNextDelete(); });
  } else {
    schedule(afterDeleteWaitMs_, [this]() { commitAndComplete(); });
  }
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
  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text-v2 complete tx=" << transactionId
                 << " commit=" << commitText_;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId);
  }
}

void SurroundingTextV2Backend::completeWithoutCommit() {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text-v2 context lost tx="
                 << transactionId;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId);
  }
}

void SurroundingTextV2Backend::schedule(uint32_t delayMs,
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

void SurroundingTextV2Backend::clearPending() {
  timer_.reset();
  inputContext_.unwatch();
  onDone_ = {};
  transactionId_ = 0;
  remainingDeletes_ = 0;
  totalDeletes_ = 0;
  deleteDelayMs_ = 0;
  afterDeleteWaitMs_ = 0;
  timerAccuracyUsec_ = 1;
  commitText_.clear();
}

} // namespace areca
