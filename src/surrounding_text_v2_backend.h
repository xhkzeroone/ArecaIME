#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <fcitx-utils/event.h>
#include <fcitx-utils/trackableobject.h>

#include "rewrite_backend.h"

namespace areca {

class SurroundingTextV2Backend final : public RewriteBackend {
public:
  using DebugProvider = std::function<bool()>;

  SurroundingTextV2Backend(fcitx::EventLoop &eventLoop,
                           DebugProvider debugProvider);
  ~SurroundingTextV2Backend() override;

  const char *name() const override { return "surrounding-text-v2"; }
  ApplyStatus apply(fcitx::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

  bool hasPending() const { return transactionId_ != 0; }

private:
  void beginIncrementalDelete();
  void sendNextDelete();
  void scheduleCommit();
  void commitAndComplete();
  void completeWithoutCommit();
  void schedule(uint32_t delayMs, std::function<void()> callback);
  void clearPending();

  fcitx::EventLoop &eventLoop_;
  DebugProvider debugProvider_;
  std::unique_ptr<fcitx::EventSourceTime> timer_;
  fcitx::TrackableObjectReference<fcitx::InputContext> inputContext_;
  RewriteDone onDone_;
  uint64_t transactionId_ = 0;
  uint32_t remainingDeletes_ = 0;
  uint32_t totalDeletes_ = 0;
  uint32_t deleteDelayMs_ = 1;
  uint32_t afterDeleteWaitMs_ = 3;
  uint64_t timerAccuracyUsec_ = 1;
  std::string commitText_;
};

} // namespace areca
