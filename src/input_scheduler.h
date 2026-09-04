#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <fcitx-utils/event.h>

#include "bamboo_engine_adapter.h"
#include "key_queue.h"
#include "rewrite_backend.h"

namespace areca {

struct SchedulerTiming {
  uint32_t backspaceDelayMs = 1;
  uint32_t waylandBackspaceDelayMs = 0;
  uint32_t afterBackspaceWaitMs = 10;
  uint32_t waylandAfterBackspaceWaitMs = 3;
  uint32_t ximAfterBackspaceWaitMs = 10;
  uint32_t fcitx4AfterBackspaceWaitMs = 10;
  uint32_t dbusAfterBackspaceWaitMs = 5;
  uint32_t surroundingWaitMs = 3;
  uint32_t surroundingDeleteDelayMs = 10;
  uint32_t waylandSurroundingDeleteDelayMs = 0;
  uint32_t postCommitDelayMs = 20;
  uint64_t timerAccuracyUsec = 1;
};

struct RewriteBackendSelection {
  RewriteBackend *backend = nullptr;
  uint32_t additionalBackspaces = 0;
};

class InputScheduler {
public:
  using EngineResolver =
      std::function<VietnameseEngine *(fcitx::InputContext &)>;
  using TimingProvider = std::function<SchedulerTiming()>;
  using DebugProvider = std::function<bool()>;
  using RewriteBackendSelector = std::function<RewriteBackendSelection(
      fcitx::InputContext &, const BambooResult &)>;

  InputScheduler(fcitx::EventLoop &eventLoop, EngineResolver engineResolver,
                 TimingProvider timingProvider, DebugProvider debugProvider,
                 RewriteBackendSelector rewriteBackendSelector);

  void enqueue(fcitx::InputContext &inputContext, uint32_t codepoint,
               std::string utf8Text);
  void enqueueBackspace(fcitx::InputContext &inputContext);
  void resetContext(fcitx::InputContext &inputContext);

  size_t queuedKeyCount() const { return queue_.size(); }
  bool rewritePending() const { return activeTransactionId_ != 0; }
  bool stalled() const { return stalled_; }
  bool shouldRejectReset() const;

private:
  void scheduleNext();
  void processNext();
  void applyResult(fcitx::InputContext &inputContext, VietnameseEngine &engine,
                   const BambooResult &result, const std::string &rawText);
  void finishKey();
  void finishKeyAfterCommit();
  void rewriteDone(uint64_t transactionId);

  fcitx::EventLoop &eventLoop_;
  EngineResolver engineResolver_;
  TimingProvider timingProvider_;
  DebugProvider debugProvider_;
  RewriteBackendSelector rewriteBackendSelector_;
  KeyQueue queue_;
  std::unique_ptr<fcitx::EventSourceTime> postCommitTimer_;
  uint64_t nextSequence_ = 1;
  uint64_t nextTransactionId_ = 1;
  uint64_t activeTransactionId_ = 0;
  uint64_t lastRewriteCompletionTimeUsec_ = 0;
  bool processing_ = false;
  bool stalled_ = false;
};

} // namespace areca
