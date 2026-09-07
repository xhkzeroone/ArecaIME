#include "surrounding_text_backend.h"

#include <utility>

#include <fcitx-utils/event.h>
#include <fcitx-utils/log.h>

#include "surrounding_text_cache.h"

namespace areca {

SurroundingTextBackend::SurroundingTextBackend(fcitx::EventLoop &eventLoop,
                                               DebugProvider debugProvider)
    : eventLoop_(eventLoop), debugProvider_(std::move(debugProvider)) {}

SurroundingTextBackend::~SurroundingTextBackend() { clearPending(); }

ApplyStatus SurroundingTextBackend::apply(fcitx::InputContext &inputContext,
                                          const RewritePlan &plan,
                                          RewriteDone onDone) {
  if (hasPending() || !plan.transactionId) {
    return ApplyStatus::Failed;
  }

  if (plan.backspaceCount) {
    inputContext.deleteSurroundingText(-static_cast<int>(plan.backspaceCount),
                                       plan.backspaceCount);
    updateSurroundingCacheAfterDelete(
        inputContext, -static_cast<int>(plan.backspaceCount),
        plan.backspaceCount);
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

  transactionId_ = plan.transactionId;
  inputContext_ = inputContext.watch();
  onDone_ = std::move(onDone);
  waitMs_ = plan.surroundingWaitMs;
  timerAccuracyUsec_ = plan.timerAccuracyUsec;
  commitText_ = plan.commitText;

  if (debugProvider_()) {
    const char *frontend = inputContext.frontend();
    FCITX_INFO() << "areca: surrounding-text start tx=" << transactionId_
                 << " delete=" << plan.backspaceCount
                 << " wait_ms=" << waitMs_
                 << " frontend=" << (frontend ? frontend : "");
  }

  scheduleCommit();
  return ApplyStatus::Pending;
}

void SurroundingTextBackend::scheduleCommit() {
  schedule(waitMs_, [this]() { commitAndComplete(); });
}

void SurroundingTextBackend::commitAndComplete() {
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
    FCITX_INFO() << "areca: surrounding-text complete tx=" << transactionId
                 << " commit=" << commitText_;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId);
  }
}

void SurroundingTextBackend::completeWithoutCommit() {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text context lost tx=" << transactionId;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId);
  }
}

void SurroundingTextBackend::schedule(uint32_t delayMs,
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

void SurroundingTextBackend::clearPending() {
  timer_.reset();
  inputContext_.unwatch();
  onDone_ = {};
  transactionId_ = 0;
  waitMs_ = 0;
  timerAccuracyUsec_ = 1;
  commitText_.clear();
}

} // namespace areca
