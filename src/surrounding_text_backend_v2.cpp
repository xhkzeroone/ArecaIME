#include "surrounding_text_backend_v2.h"

#include <utility>

#include <fcitx-utils/event.h>
#include <fcitx-utils/log.h>

#include "surrounding_text_cache.h"

namespace areca {

SurroundingTextBackendV2::SurroundingTextBackendV2(fcitx::EventLoop &eventLoop,
                                                   DebugProvider debugProvider)
    : eventLoop_(eventLoop), debugProvider_(std::move(debugProvider)) {}

SurroundingTextBackendV2::~SurroundingTextBackendV2() { clearPending(); }

ApplyStatus SurroundingTextBackendV2::apply(fcitx::InputContext &inputContext,
                                              const RewritePlan &plan,
                                              RewriteDone onDone) {
  if (hasPending() || !plan.transactionId) {
    return ApplyStatus::Failed;
  }

  if (plan.backspaceCount == 0) {
    if (!plan.commitText.empty()) {
      inputContext.commitString(plan.commitText);
      updateSurroundingCacheAfterCommit(inputContext, plan.commitText);
    }
    if (onDone) {
      onDone(plan.transactionId);
    }
    return ApplyStatus::Completed;
  }

  const char *frontend = inputContext.frontend();
  transactionId_ = plan.transactionId;
  inputContext_ = inputContext.watch();
  onDone_ = std::move(onDone);
  remainingBackspaces_ = plan.backspaceCount;
  deleteDelayMs_ = plan.surroundingDeleteDelayMs;
  waitMs_ = plan.surroundingWaitMs;
  timerAccuracyUsec_ = plan.timerAccuracyUsec;
  commitText_ = plan.commitText;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text-v2 start tx=" << transactionId_
                 << " delete=" << plan.backspaceCount
                 << " delete_delay_ms=" << deleteDelayMs_
                 << " wait_ms=" << waitMs_
                 << " frontend=" << (frontend ? frontend : "");
  }

  sendNextDelete();
  return ApplyStatus::Pending;
}

void SurroundingTextBackendV2::sendNextDelete() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  inputContext->deleteSurroundingText(-1, 1);
  if (inputContext->surroundingText().isValid()) {
    inputContext->surroundingText().deleteText(-1, 1);
  }
  --remainingBackspaces_;

  if (remainingBackspaces_) {
    schedule(deleteDelayMs_, [this]() { sendNextDelete(); });
  } else {
    scheduleCommit();
  }
}

void SurroundingTextBackendV2::scheduleCommit() {
  schedule(waitMs_, [this]() { commitAndComplete(); });
}

void SurroundingTextBackendV2::commitAndComplete() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  if (!commitText_.empty()) {
    inputContext->commitString(commitText_);
    updateSurroundingCacheAfterCommit(*inputContext, commitText_);
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

void SurroundingTextBackendV2::completeWithoutCommit() {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text-v2 context lost tx=" << transactionId;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId);
  }
}

void SurroundingTextBackendV2::schedule(uint32_t delayMs,
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

void SurroundingTextBackendV2::clearPending() {
  timer_.reset();
  inputContext_.unwatch();
  onDone_ = {};
  transactionId_ = 0;
  remainingBackspaces_ = 0;
  deleteDelayMs_ = 0;
  waitMs_ = 0;
  timerAccuracyUsec_ = 1;
  commitText_.clear();
}

} // namespace areca
