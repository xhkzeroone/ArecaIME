#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <fcitx-utils/event.h>
#include <fcitx-utils/trackableobject.h>

#include "adaptive_wait.h"
#include "event_loop_post.h"
#include "rewrite_backend.h"

namespace areca {

class SurroundingTextV2Backend final : public RewriteBackend {
public:
  using DebugProvider = std::function<bool()>;
  using ProbeSucceededHandler = std::function<void(fcitx::InputContext &)>;
  using ProbeFailedHandler = std::function<void(fcitx::InputContext &)>;
  using FallbackBackendProvider =
      std::function<RewriteBackend *(fcitx::InputContext &)>;

  SurroundingTextV2Backend(
      fcitx::EventLoop &eventLoop, DebugProvider debugProvider,
      ProbeSucceededHandler onProbeSucceeded = nullptr,
      ProbeFailedHandler onProbeFailed = nullptr,
      FallbackBackendProvider fallbackBackendProvider = nullptr,
      AdaptiveWait *adaptiveWait = nullptr);
  ~SurroundingTextV2Backend() override;

  const char *name() const override { return "surrounding-text-v2"; }
  ApplyStatus apply(fcitx::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

  bool hasPending() const { return transactionId_ != 0; }
  bool isProbing() const { return isProbe_; }
  void notifySurroundingTextUpdated(fcitx::InputContext &inputContext);

private:
  enum class TimerDispatch { TimerCallback, PostEvent };

  void beginIncrementalDelete();
  void sendNextDelete();
  void scheduleCommit();
  void commitAfterAdaptiveWait(uint32_t appliedExtraWaitMs);
  void commitAndComplete();
  void completeWithoutCommit();
  void schedule(uint32_t delayMs, TimerDispatch dispatch,
                std::function<void()> callback);
  void dispatchPostEvent(uint64_t deadlineUsec, std::function<void()> callback);
  void observeAndRun(uint64_t deadlineUsec, std::function<void()> callback);
  void scheduleProbeDeadline();
  void handleProbeFailure();
  void clearPending();

  fcitx::EventLoop &eventLoop_;
  EventLoopPostTask commitPost_;
  AdaptiveWait *adaptiveWait_ = nullptr;
  DebugProvider debugProvider_;
  ProbeSucceededHandler onProbeSucceeded_;
  ProbeFailedHandler onProbeFailed_;
  FallbackBackendProvider fallbackBackendProvider_;
  std::unique_ptr<fcitx::EventSourceTime> timer_;
  fcitx::TrackableObjectReference<fcitx::InputContext> inputContext_;
  RewriteDone onDone_;
  uint64_t transactionId_ = 0;
  uint32_t remainingDeletes_ = 0;
  uint32_t totalDeletes_ = 0;
  uint32_t deleteDelayMs_ = 10;
  uint32_t afterDeleteWaitMs_ = 1;
  uint64_t timerAccuracyUsec_ = 1;
  std::string commitText_;
  bool isProbe_ = false;
  bool probeDeletePending_ = false;
  RewritePlan savedPlan_;
  std::string originalProbeText_;
  unsigned int originalProbeCursor_ = 0;
  std::string expectedProbeText_;
  unsigned int expectedProbeCursor_ = 0;
  unsigned int expectedProbeAnchor_ = 0;
  uint32_t probeUpdateCount_ = 0;
};

} // namespace areca
