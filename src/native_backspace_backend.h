#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <fcitx-utils/event.h>
#include <fcitx-utils/trackableobject.h>

#include "adaptive_wait.h"
#include "event_loop_post.h"
#include "native_keyboard_device.h"
#include "rewrite_backend.h"

namespace areca {

class NativeBackspaceBackend final : public RewriteBackend {
public:
  using DebugProvider = std::function<bool()>;

  NativeBackspaceBackend(fcitx::EventLoop &eventLoop,
                         NativeKeyboardDevice &device,
                         AdaptiveWait &adaptiveWait,
                         DebugProvider debugProvider);
  ~NativeBackspaceBackend() override;

  const char *name() const override { return "native-backspace"; }
  ApplyStatus apply(fcitx::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

  bool isAvailable();
  bool hasPending() const { return transactionId_ != 0; }

private:
  enum class TimerDispatch { TimerCallback, PostEvent };

  bool sendNextBackspace(bool notifyFailure = true);
  void failTransaction(bool notifyFailure);
  void scheduleNextBackspace();
  void scheduleCommit();
  void commitAfterAdaptiveWait(uint32_t appliedExtraWaitMs);
  void commitAndComplete();
  void completeWithoutCommit();
  void schedule(uint32_t delayMs, TimerDispatch dispatch,
                std::function<void()> callback);
  void dispatchPostEvent(uint64_t deadlineUsec, std::function<void()> callback);
  void observeAndRun(uint64_t deadlineUsec, std::function<void()> callback);
  void clearPending();

  fcitx::EventLoop &eventLoop_;
  NativeKeyboardDevice &device_;
  EventLoopPostTask commitPost_;
  AdaptiveWait &adaptiveWait_;
  DebugProvider debugProvider_;

  std::unique_ptr<fcitx::EventSourceTime> timer_;
  fcitx::TrackableObjectReference<fcitx::InputContext> inputContext_;
  RewriteDone onDone_;
  uint64_t transactionId_ = 0;
  uint32_t remainingBackspaces_ = 0;
  uint32_t sentBackspaces_ = 0;
  uint32_t backspaceDelayMs_ = 0;
  uint32_t afterBackspaceWaitMs_ = 0;
  uint64_t timerAccuracyUsec_ = 1;
  std::string commitText_;
};

} // namespace areca
