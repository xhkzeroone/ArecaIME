#include "input_scheduler.h"

#include <exception>
#include <utility>

#include <fcitx-utils/log.h>

#include "surrounding_text_cache.h"

namespace areca {

InputScheduler::InputScheduler(fcitx::EventLoop &eventLoop,
                               EngineResolver engineResolver,
                               TimingProvider timingProvider,
                               DebugProvider debugProvider,
                               RewriteBackendSelector rewriteBackendSelector)
    : eventLoop_(eventLoop), engineResolver_(std::move(engineResolver)),
      timingProvider_(std::move(timingProvider)),
      debugProvider_(std::move(debugProvider)),
      rewriteBackendSelector_(std::move(rewriteBackendSelector)) {}

void InputScheduler::enqueue(fcitx::InputContext &inputContext,
                             uint32_t codepoint, std::string utf8Text) {
  QueuedKey key;
  key.sequence = nextSequence_++;
  key.codepoint = codepoint;
  key.utf8Text = std::move(utf8Text);
  key.inputContext = inputContext.watch();
  if (debugProvider_()) {
    FCITX_INFO() << "areca: queue push kind=text seq=" << key.sequence
                 << " text=" << key.utf8Text
                 << " depth_before=" << queue_.size();
  }
  queue_.push(std::move(key));
  scheduleNext();
}

void InputScheduler::enqueueBackspace(fcitx::InputContext &inputContext) {
  QueuedKey key;
  key.sequence = nextSequence_++;
  key.isBackspace = true;
  key.inputContext = inputContext.watch();
  if (debugProvider_()) {
    FCITX_INFO() << "areca: queue push kind=backspace seq=" << key.sequence
                 << " depth_before=" << queue_.size();
  }
  queue_.push(std::move(key));
  scheduleNext();
}

bool InputScheduler::shouldRejectReset() const {
  if (processing_ || rewritePending()) {
    return true;
  }
  if (lastRewriteCompletionTimeUsec_ != 0) {
    const uint64_t now = fcitx::now(CLOCK_MONOTONIC);
    if (now >= lastRewriteCompletionTimeUsec_ &&
        (now - lastRewriteCompletionTimeUsec_) <= 300000) {
      return true;
    }
  }
  return false;
}

void InputScheduler::resetContext(fcitx::InputContext &inputContext) {
  if (shouldRejectReset()) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: reset rejected (active rewrite or 300ms post-commit window)";
    }
    return;
  }

  queue_.removeFor(inputContext);
  if (auto *engine = engineResolver_(inputContext)) {
    engine->reset();
  }
  scheduleNext();
}

void InputScheduler::scheduleNext() {
  if (processing_ || queue_.empty() || stalled_) {
    return;
  }
  if (debugProvider_()) {
    FCITX_INFO() << "areca: scheduler pump depth=" << queue_.size();
  }
  processNext();
}

void InputScheduler::processNext() {
  if (processing_ || queue_.empty() || stalled_) {
    return;
  }

  auto key = queue_.pop();
  auto *inputContext = key.inputContext.get();
  if (!inputContext) {
    scheduleNext();
    return;
  }
  auto *engine = engineResolver_(*inputContext);
  if (!engine) {
    scheduleNext();
    return;
  }

  processing_ = true;
  if (debugProvider_()) {
    FCITX_INFO() << "areca: scheduler process seq=" << key.sequence
                 << " kind=" << (key.isBackspace ? "backspace" : "text")
                 << " depth_after=" << queue_.size();
  }
  try {
    if (key.isBackspace) {
      applyResult(*inputContext, *engine, engine->processBackspace(), "");
    } else {
      applyResult(*inputContext, *engine,
                  engine->process(key.codepoint, key.utf8Text), key.utf8Text);
    }
  } catch (const std::exception &error) {
    FCITX_ERROR() << "areca: Bamboo processing failed: " << error.what();
    engine->reset();
    finishKey();
  }
}

void InputScheduler::applyResult(fcitx::InputContext &inputContext,
                                 VietnameseEngine &engine,
                                 const BambooResult &result,
                                 const std::string &rawText) {
  if (debugProvider_()) {
    FCITX_INFO() << "areca: bamboo result current=" << result.currentText
                 << " new=" << result.newText
                 << " delete=" << result.deleteCount
                 << " commit=" << result.commitText
                 << " macro=" << result.macroExpanded;
  }
  if (!result.deleteCount) {
    // The original event was accepted before queueing, so every no-delete
    // result is applied through the input-method protocol. This also covers
    // output tables that transform a single key without replacing old text.
    if (!result.commitText.empty()) {
      inputContext.commitString(result.commitText);
      updateSurroundingCacheAfterCommit(inputContext, result.commitText);
    }
    if (debugProvider_()) {
      FCITX_INFO() << "areca: apply no-delete commit=" << result.commitText
                   << " raw=" << rawText
                   << " transformed=" << (result.commitText != rawText);
    }
    finishKeyAfterCommit();
    return;
  }

  const auto timing = timingProvider_();
  RewritePlan plan;
  plan.transactionId = nextTransactionId_++;
  plan.backspaceDelayMs = timing.backspaceDelayMs;
  plan.afterBackspaceWaitMs = timing.afterBackspaceWaitMs;
  plan.waylandAfterBackspaceWaitMs = timing.waylandAfterBackspaceWaitMs;
  plan.ximAfterBackspaceWaitMs = timing.ximAfterBackspaceWaitMs;
  plan.fcitx4AfterBackspaceWaitMs = timing.fcitx4AfterBackspaceWaitMs;
  plan.dbusAfterBackspaceWaitMs = timing.dbusAfterBackspaceWaitMs;
  plan.timerAccuracyUsec = timing.timerAccuracyUsec;
  plan.commitText = result.commitText;

  const auto selection = rewriteBackendSelector_(inputContext, result);
  if (!selection.backend) {
    FCITX_ERROR() << "areca: rewrite backend selector returned null";
    engine.reset();
    activeTransactionId_ = 0;
    finishKey();
    return;
  }
  auto &backend = *selection.backend;
  plan.backspaceCount = result.deleteCount + selection.additionalBackspaces;
  if (debugProvider_()) {
    FCITX_INFO() << "areca: rewrite select backend=" << backend.name()
                 << " tx=" << plan.transactionId
                 << " bamboo_delete=" << result.deleteCount
                 << " additional_backspaces="
                 << selection.additionalBackspaces
                 << " plan_backspaces=" << plan.backspaceCount;
  }

  activeTransactionId_ = plan.transactionId;
  const auto status =
      backend.apply(inputContext, plan, [this](uint64_t transactionId) {
        rewriteDone(transactionId);
      });
  if (debugProvider_()) {
    FCITX_INFO() << "areca: rewrite apply backend=" << backend.name()
                 << " tx=" << plan.transactionId
                 << " status=" << static_cast<int>(status);
  }
  if (status == ApplyStatus::Completed) {
    activeTransactionId_ = 0;
    finishKeyAfterCommit();
  } else if (status == ApplyStatus::Failed) {
    FCITX_ERROR() << "areca: rewrite backend failed tx=" << plan.transactionId
                  << " backend=" << backend.name();
    // Fail closed because a backend failure leaves the application state
    // unknown. Processing another key could violate ordering.
    stalled_ = true;
  }
}

void InputScheduler::rewriteDone(uint64_t transactionId) {
  if (debugProvider_()) {
    FCITX_INFO() << "areca: rewrite done tx=" << transactionId
                 << " active=" << activeTransactionId_;
  }
  if (!processing_ || transactionId != activeTransactionId_) {
    return;
  }
  activeTransactionId_ = 0;
  // Start the settle delay from the backend's actual completion barrier.
  finishKeyAfterCommit();
}

void InputScheduler::finishKey() {
  processing_ = false;
  scheduleNext();
}

void InputScheduler::finishKeyAfterCommit() {
  lastRewriteCompletionTimeUsec_ = fcitx::now(CLOCK_MONOTONIC);
  const auto timing = timingProvider_();
  const uint64_t delayUsec =
      static_cast<uint64_t>(timing.postCommitDelayMs) * 1000;
  if (debugProvider_()) {
    FCITX_INFO() << "areca: post-commit barrier delay_us=" << delayUsec
                 << " accuracy_us=" << timing.timerAccuracyUsec
                 << " queue_depth=" << queue_.size();
  }
  if (delayUsec == 0) {
    finishKey();
    return;
  }

  const uint64_t deadline = fcitx::now(CLOCK_MONOTONIC) + delayUsec;
  postCommitTimer_ = eventLoop_.addTimeEvent(
      CLOCK_MONOTONIC, deadline, timing.timerAccuracyUsec,
      [this](fcitx::EventSourceTime *, uint64_t) {
        auto completedTimer = std::move(postCommitTimer_);
        finishKey();
        return false;
      });
  if (!postCommitTimer_) {
    finishKey();
    return;
  }
  postCommitTimer_->setOneShot();
}

} // namespace areca
