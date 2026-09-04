#include "forward_backspace_backend.h"

#include <string_view>
#include <utility>

#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>

namespace areca {

ForwardBackspaceBackend::ForwardBackspaceBackend(fcitx::EventLoop &eventLoop,
                                                 DebugProvider debugProvider)
    : eventLoop_(eventLoop), debugProvider_(std::move(debugProvider)) {}

ForwardBackspaceBackend::~ForwardBackspaceBackend() { clearPending(); }

ApplyStatus ForwardBackspaceBackend::apply(fcitx::InputContext &inputContext,
                                           const RewritePlan &plan,
                                           RewriteDone onDone) {
  if (hasPending() || !plan.transactionId) {
    return ApplyStatus::Failed;
  }

  transactionId_ = plan.transactionId;
  inputContext_ = inputContext.watch();
  onDone_ = std::move(onDone);
  remainingBackspaces_ = plan.backspaceCount;
  sentBackspaces_ = 0;
  const char *frontend = inputContext.frontend();
  const bool isWayland =
      frontend && std::string_view(frontend).find("wayland") != std::string_view::npos;
  backspaceDelayMs_ = isWayland ? plan.waylandBackspaceDelayMs : plan.backspaceDelayMs;
  afterBackspaceWaitMs_ = resolveAfterBackspaceWaitMs(frontend, plan);
  timerAccuracyUsec_ = plan.timerAccuracyUsec;
  commitText_ = plan.commitText;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: forward-backspace start tx=" << transactionId_
                 << " backspaces=" << remainingBackspaces_
                 << " delay_ms=" << backspaceDelayMs_
                 << " after_wait_ms=" << afterBackspaceWaitMs_
                 << " frontend=" << (frontend ? frontend : "")
                 << " accuracy_us=" << timerAccuracyUsec_;
  }

  if (!remainingBackspaces_) {
    scheduleCommit();
  } else {
    sendNextBackspace();
  }
  return ApplyStatus::Pending;
}

void ForwardBackspaceBackend::sendNextBackspace() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  const fcitx::Key backspace(FcitxKey_BackSpace);
  inputContext->forwardKey(backspace, false);
  inputContext->forwardKey(backspace, true);
  --remainingBackspaces_;
  ++sentBackspaces_;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: forward-backspace sent tx=" << transactionId_
                 << " seen=" << sentBackspaces_
                 << " remaining=" << remainingBackspaces_;
  }

  if (remainingBackspaces_) {
    scheduleNextBackspace();
  } else {
    scheduleCommit();
  }
}

void ForwardBackspaceBackend::scheduleNextBackspace() {
  schedule(backspaceDelayMs_, [this]() { sendNextBackspace(); });
}

void ForwardBackspaceBackend::scheduleCommit() {
  schedule(afterBackspaceWaitMs_, [this]() { commitAndComplete(); });
}

void ForwardBackspaceBackend::commitAndComplete() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  if (!commitText_.empty()) {
    inputContext->commitString(commitText_);
  }

  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (debugProvider_()) {
    FCITX_INFO() << "areca: forward-backspace complete tx=" << transactionId
                 << " sent=" << sentBackspaces_ << " commit=" << commitText_;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId);
  }
}

void ForwardBackspaceBackend::completeWithoutCommit() {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (debugProvider_()) {
    FCITX_INFO() << "areca: forward-backspace context lost tx="
                 << transactionId;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId);
  }
}

void ForwardBackspaceBackend::schedule(uint32_t delayMs,
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

void ForwardBackspaceBackend::clearPending() {
  timer_.reset();
  inputContext_.unwatch();
  onDone_ = {};
  transactionId_ = 0;
  remainingBackspaces_ = 0;
  sentBackspaces_ = 0;
  backspaceDelayMs_ = 0;
  afterBackspaceWaitMs_ = 0;
  timerAccuracyUsec_ = 1;
  commitText_.clear();
}

} // namespace areca
