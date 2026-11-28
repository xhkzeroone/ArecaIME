#include "surrounding_text_backend.h"

#include <algorithm>
#include <utility>

#include <fcitx-utils/event.h>
#include <fcitx-utils/log.h>
#include <fcitx/inputcontext.h>

#include "surrounding_snapshot.h"

namespace areca {

SurroundingTextBackend::SurroundingTextBackend(
    fcitx::EventLoop &eventLoop, DebugProvider debugProvider,
    DeleteCheckHandler onDeleteChecked)
    : eventLoop_(eventLoop), commitPost_(eventLoop),
      debugProvider_(std::move(debugProvider)),
      onDeleteChecked_(std::move(onDeleteChecked)) {}

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

  transactionId_ = plan.transactionId;
  inputContext_ = inputContext.watch();
  onDone_ = std::move(onDone);
  backspaceCount_ = plan.backspaceCount;
  commitText_ = plan.commitText;
  expectedTail_ = plan.expectedTextBeforeCursor;
  pollMs_ = std::max<uint32_t>(1, plan.surroundingPollMs);
  timerAccuracyUsec_ = plan.timerAccuracyUsec;
  checkStartUsec_ = fcitx::now(CLOCK_MONOTONIC);
  checkDeadlineUsec_ =
      checkStartUsec_ +
      static_cast<uint64_t>(plan.surroundingCheckWaitMs) * 1000;
  pollCount_ = 0;
  verifyDelete_ = plan.verifySurroundingDelete;
  deleteCheckWaitMs_ = plan.surroundingDeleteCheckWaitMs;

  if (debugProvider_()) {
    const char *frontend = inputContext.frontend();
    FCITX_INFO() << "areca: surrounding-text start tx=" << transactionId_
                 << " delete=" << backspaceCount_
                 << " expected_tail=" << expectedTail_
                 << " check_wait_ms=" << plan.surroundingCheckWaitMs
                 << " poll_ms=" << pollMs_ << " verify_delete=" << verifyDelete_
                 << " frontend=" << (frontend ? frontend : "");
  }

  // Luôn trả Pending và đi tiếp trên event loop, kể cả khi snapshot đã khớp,
  // để scheduler chỉ nhận một đường hoàn tất qua onDone.
  pollSnapshot();
  return ApplyStatus::Pending;
}

void SurroundingTextBackend::pollSnapshot() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    finish(RewriteOutcome::Succeeded);
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
    if (!matched) {
      commitPost_.schedule([this]() { finish(RewriteOutcome::NeedsFallback); });
    } else if (verifyDelete_) {
      commitPost_.schedule([this]() { deleteAndVerify(); });
    } else {
      commitPost_.schedule([this]() { deleteAndCommit(); });
    }
    return;
  }
  if (!schedulePoll(nowUsec, checkDeadlineUsec_,
                    &SurroundingTextBackend::pollSnapshot)) {
    commitPost_.schedule([this]() { finish(RewriteOutcome::NeedsFallback); });
  }
}

bool SurroundingTextBackend::schedulePoll(
    uint64_t nowUsec, uint64_t deadlineUsec,
    void (SurroundingTextBackend::*poll)()) {
  const uint64_t nextUsec =
      std::min(deadlineUsec, nowUsec + static_cast<uint64_t>(pollMs_) * 1000);
  timer_ =
      eventLoop_.addTimeEvent(CLOCK_MONOTONIC, nextUsec, timerAccuracyUsec_,
                              [this, poll](fcitx::EventSourceTime *, uint64_t) {
                                auto firedTimer = std::move(timer_);
                                (this->*poll)();
                                return false;
                              });
  if (!timer_) {
    return false;
  }
  timer_->setOneShot();
  return true;
}

void SurroundingTextBackend::deleteAndCommit() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    finish(RewriteOutcome::Succeeded);
    return;
  }
  // Snapshot vừa khớp nên frontend đổi số ký tự ra byte đúng.
  inputContext->deleteSurroundingText(-static_cast<int>(backspaceCount_),
                                      backspaceCount_);
  commitAndFinish(*inputContext);
}

void SurroundingTextBackend::commitAndFinish(
    fcitx::InputContext &inputContext) {
  if (!commitText_.empty()) {
    inputContext.commitString(commitText_);
  }
  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text complete tx=" << transactionId_
                 << " commit=" << commitText_;
  }
  finish(RewriteOutcome::Succeeded);
}

void SurroundingTextBackend::deleteAndVerify() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    finish(RewriteOutcome::Succeeded);
    return;
  }
  const auto &original = inputContext->surroundingText();
  originalText_ = original.text();
  originalCursor_ = original.cursor();
  auto expected = original;
  expected.deleteText(-static_cast<int>(backspaceCount_), backspaceCount_);
  expectedText_ = expected.text();
  expectedCursor_ = expected.cursor();
  checkStartUsec_ = fcitx::now(CLOCK_MONOTONIC);
  checkDeadlineUsec_ =
      checkStartUsec_ + static_cast<uint64_t>(deleteCheckWaitMs_) * 1000;
  pollCount_ = 0;
  inputContext->deleteSurroundingText(-static_cast<int>(backspaceCount_),
                                      backspaceCount_);
  // Lệnh xoá có thể làm frontend báo snapshot mới ngay trong lúc gọi.
  pollDelete();
}

void SurroundingTextBackend::pollDelete() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    finish(RewriteOutcome::Succeeded);
    return;
  }
  ++pollCount_;
  const auto &surrounding = inputContext->surroundingText();
  const bool matched = surroundingDeleteMatches(
      surrounding.isValid(), originalText_, originalCursor_, expectedText_,
      expectedCursor_, surrounding.text(), surrounding.cursor(),
      surrounding.anchor());
  const uint64_t nowUsec = fcitx::now(CLOCK_MONOTONIC);
  if (matched || nowUsec >= checkDeadlineUsec_) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: surrounding-text delete check tx="
                   << transactionId_ << " matched=" << matched
                   << " polls=" << pollCount_
                   << " waited_us=" << (nowUsec - checkStartUsec_)
                   << " expected_text=" << expectedText_
                   << " actual_text=" << surrounding.text()
                   << " cursor=" << surrounding.cursor()
                   << " anchor=" << surrounding.anchor();
    }
    if (onDeleteChecked_) {
      onDeleteChecked_(*inputContext, matched);
    }
    if (matched) {
      commitPost_.schedule([this]() {
        auto *inputContext = inputContext_.get();
        if (!inputContext) {
          finish(RewriteOutcome::Succeeded);
          return;
        }
        commitAndFinish(*inputContext);
      });
    } else {
      // Ứng dụng không làm theo lệnh xoá nên chữ cũ vẫn còn: chạy lại lượt này
      // bằng Backspace.
      commitPost_.schedule([this]() { finish(RewriteOutcome::NeedsFallback); });
    }
    return;
  }
  if (!schedulePoll(nowUsec, checkDeadlineUsec_,
                    &SurroundingTextBackend::pollDelete)) {
    if (onDeleteChecked_) {
      onDeleteChecked_(*inputContext, false);
    }
    commitPost_.schedule([this]() { finish(RewriteOutcome::NeedsFallback); });
  }
}

void SurroundingTextBackend::finish(RewriteOutcome outcome) {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (debugProvider_()) {
    if (!inputContext_.get()) {
      FCITX_INFO() << "areca: surrounding-text context lost tx="
                   << transactionId;
    } else if (outcome == RewriteOutcome::NeedsFallback) {
      FCITX_INFO() << "areca: surrounding-text needs fallback tx="
                   << transactionId;
    }
  }
  clearPending();
  if (onDone) {
    onDone(transactionId, outcome);
  }
}

void SurroundingTextBackend::clearPending() {
  timer_.reset();
  commitPost_.cancel();
  inputContext_.unwatch();
  onDone_ = {};
  transactionId_ = 0;
  backspaceCount_ = 0;
  commitText_.clear();
  expectedTail_.clear();
  pollMs_ = 1;
  timerAccuracyUsec_ = 1;
  checkStartUsec_ = 0;
  checkDeadlineUsec_ = 0;
  pollCount_ = 0;
  verifyDelete_ = false;
  deleteCheckWaitMs_ = 0;
  originalText_.clear();
  originalCursor_ = 0;
  expectedText_.clear();
  expectedCursor_ = 0;
}

} // namespace areca
